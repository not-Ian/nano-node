#pragma once

#include <nano/crypto/blake2/blake2.h>
#include <nano/lib/blockbuilders.hpp>
#include <nano/lib/common.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/constants.hpp>
#include <nano/lib/epochs.hpp>
#include <nano/lib/fwd.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/lib/timer.hpp>
#include <nano/lib/utility.hpp>

#include <array>
#include <unordered_map>

namespace nano
{
/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (nano::raw_key &&);
	nano::public_key pub;
	nano::raw_key prv;
};

class endpoint_key final
{
public:
	endpoint_key () = default;
	endpoint_key (nano::endpoint const &);

	/*
	 * @param address_a This should be in network byte order
	 * @param port_a This should be in host byte order
	 */
	endpoint_key (std::array<uint8_t, 16> const & address_a, uint16_t port_a);

	/*
	 * @return The ipv6 address in network byte order
	 */
	std::array<uint8_t, 16> const & address_bytes () const;

	/*
	 * @return The port in host byte order
	 */
	uint16_t port () const;

	nano::endpoint endpoint () const;

private:
	// Both stored internally in network byte order
	std::array<uint8_t, 16> address;
	uint16_t network_port{ 0 };
};

enum class no_value
{
	dummy
};

class unchecked_key final
{
public:
	unchecked_key () = default;
	explicit unchecked_key (nano::hash_or_account const & dependency);
	unchecked_key (nano::hash_or_account const &, nano::block_hash const &);
	unchecked_key (nano::uint512_union const &);
	bool deserialize (nano::stream &);
	bool operator== (nano::unchecked_key const &) const;
	bool operator< (nano::unchecked_key const &) const;
	nano::block_hash const & key () const;
	nano::block_hash previous{ 0 };
	nano::block_hash hash{ 0 };
};

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<nano::block> const &);
	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);
	nano::seconds_t modified () const;
	std::shared_ptr<nano::block> block;

private:
	/** Seconds since posix epoch */
	uint64_t modified_m{ 0 };
};

class block_info final
{
public:
	block_info () = default;
	block_info (nano::account const &, nano::amount const &);
	nano::account account{};
	nano::amount balance{ 0 };
};

class confirmation_height_info final
{
public:
	confirmation_height_info () = default;
	confirmation_height_info (uint64_t, nano::block_hash const &);

	void serialize (nano::stream &) const;
	bool deserialize (nano::stream &);

	/** height of the cemented frontier */
	uint64_t height{};

	/** hash of the highest cemented block, the cemented/confirmed frontier */
	nano::block_hash frontier{};
};

namespace confirmation_height
{
	/** When the uncemented count (block count - cemented count) is less than this use the unbounded processor */
	uint64_t const unbounded_cutoff{ 16384 };
}

enum class block_status
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	gap_epoch_open_pending, // Block marked as pending blocks required for epoch open block are unknown
	opened_burn_account, // Block attempts to open the burn account
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position, // This block cannot follow the previous block
	insufficient_work // Insufficient work for this block, even though it passed the minimal validation
};

std::string_view to_string (block_status);
nano::stat::detail to_stat_detail (block_status);

enum class tally_result
{
	vote,
	changed,
	confirm
};

class network_params;

/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (nano::work_thresholds &, nano::networks);
	nano::work_thresholds & work;
	nano::keypair zero_key;
	nano::account nano_beta_account;
	nano::account nano_live_account;
	nano::account nano_test_account;
	std::shared_ptr<nano::block> nano_dev_genesis;
	std::shared_ptr<nano::block> nano_beta_genesis;
	std::shared_ptr<nano::block> nano_live_genesis;
	std::shared_ptr<nano::block> nano_test_genesis;
	std::shared_ptr<nano::block> genesis;
	nano::uint128_t genesis_amount;
	nano::account burn_account;
	nano::epochs epochs;
};

namespace dev
{
	extern nano::keypair genesis_key;
	extern nano::network_params network_params;
	extern nano::ledger_constants & constants;
	extern std::shared_ptr<nano::block> & genesis;
}

/** Constants which depend on random values (always used as singleton) */
class hardened_constants
{
public:
	static hardened_constants & get ();

	nano::account not_an_account;
	nano::uint128_union random_128;

private:
	hardened_constants ();
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	node_constants (nano::network_constants & network_constants);
	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::minutes unchecked_cleaning_interval;
	std::chrono::milliseconds process_confirmed_interval;

	/** Time between collecting online representative samples */
	std::chrono::seconds weight_interval;
	/** The maximum time to keep online weight samples: 2 weeks on live or 1 day on beta */
	std::chrono::seconds weight_cutoff;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	voting_constants (nano::network_constants & network_constants);
	size_t const max_cache;
	std::chrono::seconds const delay;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	portmapping_constants (nano::network_constants & network_constants);
	// Timeouts are primes so they infrequently happen at the same time
	std::chrono::seconds lease_duration;
	std::chrono::seconds health_check_period;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	bootstrap_constants (nano::network_constants & network_constants);
	uint32_t lazy_max_pull_blocks;
	uint32_t lazy_min_pull_blocks;
	unsigned frontier_retry_limit;
	unsigned lazy_retry_limit;
	unsigned lazy_destinations_retry_limit;
	std::chrono::milliseconds gap_cache_bootstrap_start_interval;
	uint32_t default_frontiers_age_seconds;
};

nano::work_thresholds const & work_thresholds_for_network (nano::networks);

/** Constants whose value depends on the active network */
class network_params
{
public:
	explicit network_params (nano::networks);

	unsigned kdf_work;
	nano::work_thresholds work;
	nano::network_constants network;
	nano::ledger_constants ledger;
	nano::voting_constants voting;
	nano::node_constants node;
	nano::portmapping_constants portmapping;
	nano::bootstrap_constants bootstrap;
};

nano::wallet_id random_wallet_id ();
}
