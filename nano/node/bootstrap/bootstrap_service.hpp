#pragma once

#include <nano/lib/interval.hpp>
#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/random.hpp>
#include <nano/lib/rate_limiting.hpp>
#include <nano/lib/thread_pool.hpp>
#include <nano/node/bootstrap/account_sets.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>
#include <nano/node/bootstrap/common.hpp>
#include <nano/node/bootstrap/database_scan.hpp>
#include <nano/node/bootstrap/frontier_scan.hpp>
#include <nano/node/bootstrap/peer_scoring.hpp>
#include <nano/node/bootstrap/throttle.hpp>
#include <nano/node/fwd.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <thread>

namespace mi = boost::multi_index;

namespace nano
{
class bootstrap_service
{
public:
	bootstrap_service (nano::node_config const &, nano::ledger &, nano::ledger_notifications &, nano::block_processor &, nano::network &, nano::stats &, nano::logger &);
	~bootstrap_service ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_ack` message coming from network
	 */
	void process (nano::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const &);

	std::size_t blocked_size () const;
	std::size_t priority_size () const;
	std::size_t score_size () const;

	bool prioritized (nano::account const &) const;
	bool blocked (nano::account const &) const;

	nano::container_info container_info () const;

	nano::bootstrap::account_sets::info_t info () const;

private: // Dependencies
	bootstrap_config const & config;
	nano::network_constants const & network_constants;
	nano::ledger & ledger;
	nano::ledger_notifications & ledger_notifications;
	nano::block_processor & block_processor;
	nano::network & network;
	nano::stats & stats;
	nano::logger & logger;

public: // Tag
	enum class query_type
	{
		invalid = 0, // Default initialization
		blocks_by_hash,
		blocks_by_account,
		account_info_by_hash,
		frontiers,
	};

	enum class query_source
	{
		invalid,
		priority,
		database,
		dependencies,
		frontiers,
	};

	struct async_tag
	{
		using id_t = nano::bootstrap::id_t;

		query_type type{ query_type::invalid };
		query_source source{ query_source::invalid };
		nano::hash_or_account start{ 0 };
		nano::account account{ 0 };
		nano::block_hash hash{ 0 };
		size_t count{ 0 };
		std::chrono::steady_clock::time_point cutoff{};
		std::chrono::steady_clock::time_point timestamp{ std::chrono::steady_clock::now () };
		id_t id{ nano::bootstrap::generate_id () };
	};

private:
	/* Inspects a block that has been processed by the block processor */
	void inspect (secure::transaction const &, nano::block_status const & result, nano::block const & block, nano::block_source);

	void run_priorities ();
	void run_one_priority ();
	void run_database ();
	void run_one_database (bool should_throttle);
	void run_dependencies ();
	void run_one_dependency ();
	void run_frontiers ();
	void run_one_frontier ();
	void run_timeouts ();
	void cleanup_and_sync ();

	/* Waits for a condition to be satisfied with incremental backoff */
	void wait (std::function<bool ()> const & predicate) const;

	/* Ensure there is enough space in block_processor for queuing new blocks */
	void wait_block_processor () const;
	/* Waits for a channel that is not full */
	std::shared_ptr<nano::transport::channel> wait_channel ();
	/* Waits until a suitable account outside of cooldown period is available */
	using priority_result = nano::bootstrap::account_sets::priority_result;
	priority_result next_priority ();
	priority_result wait_priority ();
	/* Gets the next account from the database */
	nano::account next_database (bool should_throttle);
	nano::account wait_database (bool should_throttle);
	/* Waits for next available blocking block */
	nano::block_hash next_blocking ();
	nano::block_hash wait_blocking ();
	/* Waits for next available frontier scan range */
	nano::account wait_frontier ();

	bool request (nano::account, size_t count, std::shared_ptr<nano::transport::channel> const &, query_source);
	bool request_info (nano::block_hash, std::shared_ptr<nano::transport::channel> const &, query_source);
	bool request_frontiers (nano::account, std::shared_ptr<nano::transport::channel> const &, query_source);
	bool send (std::shared_ptr<nano::transport::channel> const &, async_tag tag);

	bool process (nano::asc_pull_ack::blocks_payload const & response, async_tag const & tag);
	bool process (nano::asc_pull_ack::account_info_payload const & response, async_tag const & tag);
	bool process (nano::asc_pull_ack::frontiers_payload const & response, async_tag const & tag);
	bool process (nano::empty_payload const & response, async_tag const & tag);

	void process_frontiers (std::deque<std::pair<nano::account, nano::block_hash>> const & frontiers);

	enum class verify_result
	{
		ok,
		nothing_new,
		invalid,
	};

	/**
	 * Verifies whether the received response is valid. Returns:
	 * - invalid: when received blocks do not correspond to requested hash/account or they do not make a valid chain
	 * - nothing_new: when received response indicates that the account chain does not have more blocks
	 * - ok: otherwise, if all checks pass
	 */
	verify_result verify (nano::asc_pull_ack::blocks_payload const & response, async_tag const & tag) const;
	verify_result verify (nano::asc_pull_ack::frontiers_payload const & response, async_tag const & tag) const;

	size_t count_tags (nano::account const & account, query_source source) const;
	size_t count_tags (nano::block_hash const & hash, query_source source) const;

	// Calculates a lookback size based on the size of the ledger where larger ledgers have a larger sample count
	std::size_t compute_throttle_size () const;

private:
	nano::bootstrap::account_sets accounts;
	nano::bootstrap::database_scan database_scan;
	nano::bootstrap::throttle throttle;
	nano::bootstrap::peer_scoring scoring;
	nano::bootstrap::frontier_scan frontiers;

	// clang-format off
	class tag_sequenced {};
	class tag_id {};
	class tag_account {};
	class tag_hash {};

	using ordered_tags = boost::multi_index_container<async_tag,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<async_tag, nano::bootstrap::id_t, &async_tag::id>>,
		mi::hashed_non_unique<mi::tag<tag_account>,
			mi::member<async_tag, nano::account , &async_tag::account>>,
		mi::hashed_non_unique<mi::tag<tag_hash>,
			mi::member<async_tag, nano::block_hash, &async_tag::hash>>
	>>;
	// clang-format on
	ordered_tags tags;

	// Rate limiter for all types of requests
	nano::rate_limiter limiter;
	// Requests for accounts from database have much lower hitrate and could introduce strain on the network
	// A separate (lower) limiter ensures that we always reserve resources for querying accounts from priority queue
	nano::rate_limiter database_limiter;
	// Rate limiter for frontier requests
	nano::rate_limiter frontiers_limiter;

	nano::interval sync_dependencies_interval;

	bool stopped{ false };
	mutable nano::mutex mutex;
	mutable nano::condition_variable condition;
	std::thread priorities_thread;
	std::thread database_thread;
	std::thread dependencies_thread;
	std::thread frontiers_thread;
	std::thread cleanup_thread;

	nano::thread_pool workers;
	nano::random_generator_mt rng;
};

nano::stat::detail to_stat_detail (bootstrap_service::query_type);
}
