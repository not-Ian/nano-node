#include <nano/node/endpoint.hpp>
#include <nano/node/node.hpp>
#include <nano/node/transport/transport.hpp>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>

nano::endpoint nano::transport::map_endpoint_to_v6 (nano::endpoint const & endpoint_a)
{
	auto endpoint_l (endpoint_a);
	if (endpoint_l.address ().is_v4 ())
	{
		endpoint_l = nano::endpoint (boost::asio::ip::address_v6::v4_mapped (endpoint_l.address ().to_v4 ()), endpoint_l.port ());
	}
	return endpoint_l;
}

nano::endpoint nano::transport::map_tcp_to_endpoint (nano::tcp_endpoint const & endpoint_a)
{
	return { endpoint_a.address (), endpoint_a.port () };
}

nano::tcp_endpoint nano::transport::map_endpoint_to_tcp (nano::endpoint const & endpoint_a)
{
	return { endpoint_a.address (), endpoint_a.port () };
}

boost::asio::ip::address nano::transport::map_address_to_subnetwork (boost::asio::ip::address address_a)
{
	address_a = mapped_from_v4_or_v6 (address_a);
	static short const ipv6_subnet_prefix_length = 32; // Equivalent to network prefix /32.
	static short const ipv4_subnet_prefix_length = (128 - 32) + 24; // Limits for /24 IPv4 subnetwork (we're using mapped IPv4 to IPv6 addresses, hence (128 - 32))
	return address_a.to_v6 ().is_v4_mapped () ? boost::asio::ip::make_network_v6 (address_a.to_v6 (), ipv4_subnet_prefix_length).network () : boost::asio::ip::make_network_v6 (address_a.to_v6 (), ipv6_subnet_prefix_length).network ();
}

boost::asio::ip::address nano::transport::ipv4_address_or_ipv6_subnet (boost::asio::ip::address address_a)
{
	address_a = mapped_from_v4_or_v6 (address_a);
	// Assuming /48 subnet prefix for IPv6 as it's relatively easy to acquire such a /48 address range
	static short const ipv6_address_prefix_length = 48; // /48 IPv6 subnetwork
	return address_a.to_v6 ().is_v4_mapped () ? address_a : boost::asio::ip::make_network_v6 (address_a.to_v6 (), ipv6_address_prefix_length).network ();
}

bool nano::transport::is_same_ip (boost::asio::ip::address const & address_a, boost::asio::ip::address const & address_b)
{
	return ipv4_address_or_ipv6_subnet (address_a) == ipv4_address_or_ipv6_subnet (address_b);
}

bool nano::transport::is_same_subnetwork (boost::asio::ip::address const & address_a, boost::asio::ip::address const & address_b)
{
	return map_address_to_subnetwork (address_a) == map_address_to_subnetwork (address_b);
}

boost::asio::ip::address_v6 nano::transport::mapped_from_v4_bytes (unsigned long address_a)
{
	return boost::asio::ip::address_v6::v4_mapped (boost::asio::ip::address_v4 (address_a));
}

boost::asio::ip::address_v6 nano::transport::mapped_from_v4_or_v6 (boost::asio::ip::address const & address_a)
{
	return address_a.is_v4 () ? boost::asio::ip::address_v6::v4_mapped (address_a.to_v4 ()) : address_a.to_v6 ();
}

bool nano::transport::is_ipv4_or_v4_mapped_address (boost::asio::ip::address const & address_a)
{
	return address_a.is_v4 () || address_a.to_v6 ().is_v4_mapped ();
}

bool nano::transport::reserved_address (nano::endpoint const & endpoint_a, bool allow_local_peers)
{
	debug_assert (endpoint_a.address ().is_v6 ());
	auto bytes (endpoint_a.address ().to_v6 ());
	auto result (false);
	static auto const rfc1700_min (mapped_from_v4_bytes (0x00000000ul));
	static auto const rfc1700_max (mapped_from_v4_bytes (0x00fffffful));
	static auto const rfc1918_1_min (mapped_from_v4_bytes (0x0a000000ul));
	static auto const rfc1918_1_max (mapped_from_v4_bytes (0x0afffffful));
	static auto const rfc1918_2_min (mapped_from_v4_bytes (0xac100000ul));
	static auto const rfc1918_2_max (mapped_from_v4_bytes (0xac1ffffful));
	static auto const rfc1918_3_min (mapped_from_v4_bytes (0xc0a80000ul));
	static auto const rfc1918_3_max (mapped_from_v4_bytes (0xc0a8fffful));
	static auto const rfc6598_min (mapped_from_v4_bytes (0x64400000ul));
	static auto const rfc6598_max (mapped_from_v4_bytes (0x647ffffful));
	static auto const rfc5737_1_min (mapped_from_v4_bytes (0xc0000200ul));
	static auto const rfc5737_1_max (mapped_from_v4_bytes (0xc00002fful));
	static auto const rfc5737_2_min (mapped_from_v4_bytes (0xc6336400ul));
	static auto const rfc5737_2_max (mapped_from_v4_bytes (0xc63364fful));
	static auto const rfc5737_3_min (mapped_from_v4_bytes (0xcb007100ul));
	static auto const rfc5737_3_max (mapped_from_v4_bytes (0xcb0071fful));
	static auto const ipv4_multicast_min (mapped_from_v4_bytes (0xe0000000ul));
	static auto const ipv4_multicast_max (mapped_from_v4_bytes (0xeffffffful));
	static auto const rfc6890_min (mapped_from_v4_bytes (0xf0000000ul));
	static auto const rfc6890_max (mapped_from_v4_bytes (0xfffffffful));
	static auto const rfc6666_min (boost::asio::ip::make_address_v6 ("100::"));
	static auto const rfc6666_max (boost::asio::ip::make_address_v6 ("100::ffff:ffff:ffff:ffff"));
	static auto const rfc3849_min (boost::asio::ip::make_address_v6 ("2001:db8::"));
	static auto const rfc3849_max (boost::asio::ip::make_address_v6 ("2001:db8:ffff:ffff:ffff:ffff:ffff:ffff"));
	static auto const rfc4193_min (boost::asio::ip::make_address_v6 ("fc00::"));
	static auto const rfc4193_max (boost::asio::ip::make_address_v6 ("fd00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	static auto const ipv6_multicast_min (boost::asio::ip::make_address_v6 ("ff00::"));
	static auto const ipv6_multicast_max (boost::asio::ip::make_address_v6 ("ff00:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
	if (endpoint_a.port () == 0)
	{
		result = true;
	}
	else if (bytes >= rfc1700_min && bytes <= rfc1700_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_1_min && bytes <= rfc5737_1_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_2_min && bytes <= rfc5737_2_max)
	{
		result = true;
	}
	else if (bytes >= rfc5737_3_min && bytes <= rfc5737_3_max)
	{
		result = true;
	}
	else if (bytes >= ipv4_multicast_min && bytes <= ipv4_multicast_max)
	{
		result = true;
	}
	else if (bytes >= rfc6890_min && bytes <= rfc6890_max)
	{
		result = true;
	}
	else if (bytes >= rfc6666_min && bytes <= rfc6666_max)
	{
		result = true;
	}
	else if (bytes >= rfc3849_min && bytes <= rfc3849_max)
	{
		result = true;
	}
	else if (bytes >= ipv6_multicast_min && bytes <= ipv6_multicast_max)
	{
		result = true;
	}
	else if (!allow_local_peers)
	{
		if (bytes >= rfc1918_1_min && bytes <= rfc1918_1_max)
		{
			result = true;
		}
		else if (bytes >= rfc1918_2_min && bytes <= rfc1918_2_max)
		{
			result = true;
		}
		else if (bytes >= rfc1918_3_min && bytes <= rfc1918_3_max)
		{
			result = true;
		}
		else if (bytes >= rfc6598_min && bytes <= rfc6598_max)
		{
			result = true;
		}
		else if (bytes >= rfc4193_min && bytes <= rfc4193_max)
		{
			result = true;
		}
	}
	return result;
}

nano::stat::detail nano::to_stat_detail (boost::system::error_code const & ec)
{
	switch (ec.value ())
	{
		case boost::system::errc::success:
			return nano::stat::detail::success;
		case boost::system::errc::no_buffer_space:
			return nano::stat::detail::no_buffer_space;
		case boost::system::errc::timed_out:
			return nano::stat::detail::timed_out;
		case boost::system::errc::host_unreachable:
			return nano::stat::detail::host_unreachable;
		case boost::system::errc::not_supported:
			return nano::stat::detail::not_supported;
		default:
			return nano::stat::detail::other;
	}
}

/*
 * socket_functions
 */

boost::asio::ip::network_v6 nano::transport::socket_functions::get_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, std::size_t network_prefix)
{
	return boost::asio::ip::make_network_v6 (ip_address, static_cast<unsigned short> (network_prefix));
}

boost::asio::ip::address nano::transport::socket_functions::first_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, std::size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (ip_address, network_prefix).hosts ();
	debug_assert (!range.empty ());
	return *(range.begin ());
}

boost::asio::ip::address nano::transport::socket_functions::last_ipv6_subnet_address (boost::asio::ip::address_v6 const & ip_address, std::size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (ip_address, network_prefix).hosts ();
	debug_assert (!range.empty ());
	return *(--range.end ());
}

std::size_t nano::transport::socket_functions::count_subnetwork_connections (
nano::transport::address_socket_mmap const & per_address_connections,
boost::asio::ip::address_v6 const & remote_address,
std::size_t network_prefix)
{
	auto range = get_ipv6_subnet_address (remote_address, network_prefix).hosts ();
	if (range.empty ())
	{
		return 0;
	}
	auto const first_ip = first_ipv6_subnet_address (remote_address, network_prefix);
	auto const last_ip = last_ipv6_subnet_address (remote_address, network_prefix);
	auto const counted_connections = std::distance (per_address_connections.lower_bound (first_ip), per_address_connections.upper_bound (last_ip));
	return counted_connections;
}