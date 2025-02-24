add_executable(
  core_test
  entry.cpp
  fakes/websocket_client.hpp
  fakes/work_peer.hpp
  active_elections.cpp
  assert.cpp
  async.cpp
  backlog.cpp
  block.cpp
  block_store.cpp
  block_processor.cpp
  bootstrap.cpp
  bootstrap_frontier_scan.cpp
  bootstrap_server.cpp
  bucketing.cpp
  cli.cpp
  confirmation_solicitor.cpp
  confirming_set.cpp
  conflicts.cpp
  difficulty.cpp
  distributed_work.cpp
  election.cpp
  election_scheduler.cpp
  enums.cpp
  epochs.cpp
  fair_queue.cpp
  ipc.cpp
  ledger.cpp
  ledger_confirm.cpp
  ledger_priority.cpp
  locks.cpp
  logging.cpp
  message.cpp
  message_deserializer.cpp
  memory_pool.cpp
  network.cpp
  network_filter.cpp
  network_functions.cpp
  node.cpp
  numbers.cpp
  object_stream.cpp
  observer_set.cpp
  online_reps.cpp
  optimistic_scheduler.cpp
  processing_queue.cpp
  processor_service.cpp
  random.cpp
  random_pool.cpp
  rate_limiting.cpp
  rep_crawler.cpp
  receivable.cpp
  peer_history.cpp
  peer_container.cpp
  rep_weight_store.cpp
  scheduler_buckets.cpp
  stats.cpp
  request_aggregator.cpp
  signal_manager.cpp
  socket.cpp
  stacktrace.cpp
  system.cpp
  tcp_listener.cpp
  telemetry.cpp
  thread_pool.cpp
  throttle.cpp
  toml.cpp
  timer.cpp
  unchecked_map.cpp
  utility.cpp
  vote_cache.cpp
  vote_processor.cpp
  voting.cpp
  wallet.cpp
  wallets.cpp
  websocket.cpp
  work_pool.cpp)

target_compile_definitions(
  core_test PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
                    -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

target_link_libraries(core_test test_common)

include_directories(${CMAKE_SOURCE_DIR}/submodules)
include_directories(${CMAKE_SOURCE_DIR}/submodules/gtest/googletest/include)
