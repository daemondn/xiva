// Copyright (C) 2009 Yandex <highpower@yandex.ru>

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#ifndef XIVA_DETAILS_CONNECTION_IMPL_HPP_INCLUDED
#define XIVA_DETAILS_CONNECTION_IMPL_HPP_INCLUDED

#include <list>
#include <iosfwd>
#include <string>
#include <exception>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include "xiva/logger.hpp"
#include "xiva/forward.hpp"
#include "xiva/message.hpp"
#include "xiva/http_error.hpp"

#include "details/asio.hpp"
#include "details/connection_base.hpp"
#include "details/connection_data.hpp"
#include "details/http.hpp"
#include "details/http_constants.hpp"
#include "details/request_impl.hpp"
#include "details/response_impl.hpp"
#include "details/request_checker.hpp"
#include "details/websocket_info.hpp"

namespace xiva { namespace details {

template <typename ConnectionBase, typename ConnectionTraits>
class connection_impl : public ConnectionBase {

public:
	connection_impl(asio::io_service &io, connection_data const &data, ConnectionTraits &ct);
	virtual ~connection_impl();

	typedef connection_impl<ConnectionBase, ConnectionTraits> type;
	typedef typename boost::intrusive_ptr<type> connection_impl_ptr_type;

	typedef boost::shared_ptr<message> message_ptr_type;

	virtual void start();
	virtual void finish();
	virtual void send(message_ptr_type const &message);

	void handle_read(syst::error_code const &code);
	void handle_write_headers(syst::error_code const &code);
	void handle_write_message(syst::error_code const &code);
	void handle_cleanup(syst::error_code const &code);
	void handle_timeout(syst::error_code const &code);
	void handle_inactive_timeout(syst::error_code const &code);

	char const* address() const;
	asio::ip::tcp::socket& socket();
	virtual void handled(response_impl const &resp);

private:
	typedef std::allocator<char> allocator_type;
	typedef asio::basic_streambuf<allocator_type> streambuf_type;

	void read();
	void write_headers(response_impl const &resp);
	void write_message();
	void write_last_message();
	void write_policy_data();
	void write_http_error(http_error const &http);

	void cleanup();
	void setup_timeout(unsigned short timeout);
	void setup_inactive_timeout();
	void handle_error(syst::error_code const &code);
	void handle_exception(std::exception const &exc);

	void print_error(std::streambuf &buf, http_error const &error) const;
	void print_headers(std::string const &content_type, std::streambuf &buf) const;

private:
	asio::io_service &io_;
	connection_data const &data_;
	ConnectionTraits &ct_;

	std::string addr_;
	bool writing_message_;
	streambuf_type in_, out_;

	asio::deadline_timer timer_;
	asio::ip::tcp::socket socket_;
	std::list<message_ptr_type> messages_;

	websocket_info ws_info_;
	bool is_policy_;
};

template <typename ConnectionBase, typename ConnectionTraits>
connection_impl<ConnectionBase, ConnectionTraits>::connection_impl(asio::io_service &io, connection_data const &data, ConnectionTraits &ct) :
	io_(io), data_(data), ct_(ct), writing_message_(false), timer_(io_), socket_(io_), is_policy_(false)
{
}

template <typename ConnectionBase, typename ConnectionTraits>
connection_impl<ConnectionBase, ConnectionTraits>::~connection_impl() {

}

template <typename ConnectionBase, typename ConnectionTraits> inline char const*
connection_impl<ConnectionBase, ConnectionTraits>::address() const {
	return addr_.c_str();
}

template <typename ConnectionBase, typename ConnectionTraits> inline asio::ip::tcp::socket&
connection_impl<ConnectionBase, ConnectionTraits>::socket() {
	return socket_;
}


template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::start() {
	asio::ip::tcp::endpoint endpoint = socket_.remote_endpoint();
	asio::ip::address addr = endpoint.address();
	addr_ = addr.to_string();
	read();
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::finish() {
	send(boost::shared_ptr<message>());
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::send(boost::shared_ptr<message> const &m) {
	messages_.push_back(m);
	if (!writing_message_) {
		write_message();
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handled(response_impl const &resp) {
	timer_.cancel();
	try {
		std::string const &name = ConnectionBase::name();
		if (name.empty()) {
			throw http_error(http_error::not_found);
		}
		boost::intrusive_ptr<ConnectionBase> self(this);
		ct_.manager().insert_connection(self);
		data_.log()->debug("name %s assigned to connection[%lu] from %s", name.c_str(), ConnectionBase::id(), address());
		write_headers(resp); 
	}
	catch (http_error const &h) {
		write_http_error(h);
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_read(syst::error_code const &code) {
	timer_.cancel();
	if (code) {
		handle_error(code);
		return;
	}
	try {
		typedef streambuf_type::const_buffers_type buffers_type;
		typedef asio::buffers_iterator<buffers_type> iterator_type;

		buffers_type data = in_.data();
		iterator_type begin = iterator_type::begin(data);
		iterator_type end = iterator_type::end(data);
		is_policy_ = data_.is_policy(begin, end);
		if (is_policy_) {
			write_policy_data();
		}
		else {
			request_impl req(begin, end);
			ws_info_.parse(req);
			
			response_impl resp;
			boost::intrusive_ptr<ConnectionBase> self(this);
			ct_.handler_invoker().invoke_handler(self, req, resp);
		}
	}
	catch (http_error const &h) {
		write_http_error(h);
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_write_headers(syst::error_code const &code) {
	timer_.cancel();
	if (code) {
		handle_error(code);
		return;
	}
	write_message();
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_write_message(syst::error_code const &code) {
	timer_.cancel();
	writing_message_ = false;
	if (code) {
		handle_error(code);
		return;
	}
	messages_.pop_front();
	write_message();
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_cleanup(syst::error_code const &code) {
	(void) code;
	cleanup();
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_timeout(syst::error_code const &code) {
	if (!code) {
		data_.log()->info("timeout was reached with connection[%lu] from %s", ConnectionBase::id(), address());
		cleanup();
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_inactive_timeout(syst::error_code const &code) {
	if (!code) {
		data_.log()->info("inactive timeout was reached with connection[%lu] from %s", ConnectionBase::id(), address());
		write_last_message();
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::read() {

	try {
		connection_impl_ptr_type self(this);
		asio::async_read_until(socket_, in_, request_checker(), 
			boost::bind(&type::handle_read, self, asio::placeholders::error));
		setup_timeout(data_.read_timeout());
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::write_headers(response_impl const &resp) {

	if (is_policy_ || (!ws_info_.empty() && !ws_info_.valid())) {
		cleanup();
		return;
	}

	try {
		print_headers(resp.content_type(), out_);
		connection_impl_ptr_type self(this);
		asio::async_write(socket_, out_, boost::bind(&type::handle_write_headers, self,
			asio::placeholders::error));
		setup_timeout(data_.write_timeout());
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::write_message() {

	if (messages_.empty()) {
		setup_inactive_timeout();
		return;
	}
	timer_.cancel();
	boost::shared_ptr<message> const &message = messages_.front();
	if (!message) {
		write_last_message();
		return;
	}

	connection_impl_ptr_type self(this);
	try {
		std::ostream stream(&out_);
		std::string const &content = message->content();
		if (!ws_info_.empty()) {
			ws_info_.write_message(stream, content);
		}
		else {
			stream << std::hex << content.size() << http_constants<char>::endl;
			stream << content << http_constants<char>::endl;
		}
		writing_message_ = true;
		asio::async_write(socket_, out_, boost::bind(&type::handle_write_message,
			self, asio::placeholders::error));
		setup_timeout(data_.write_timeout());
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::write_last_message() {

	if (is_policy_ || !ws_info_.empty()) {
		cleanup();
		return;
	}

	try {
		std::ostream stream(&out_);
		stream << 0 << http_constants<char>::endl;
		writing_message_ = true;
		connection_impl_ptr_type self(this);
		asio::async_write(socket_, out_, boost::bind(&type::handle_cleanup,
			self, asio::placeholders::error));
		setup_timeout(data_.write_timeout());
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::write_policy_data() {

	data_.log()->debug("policy request for connection[%lu] from %s", ConnectionBase::id(), address());
	if (data_.policy_data().empty()) {
		data_.log()->error("can not answer policy data for connection[%lu] from %s", ConnectionBase::id(), address());
		cleanup();
		return;
	}

	try {
		std::ostream stream(&out_);
		stream << data_.policy_data() << '\0';
		writing_message_ = true;
		connection_impl_ptr_type self(this);
		asio::async_write(socket_, out_, boost::bind(&type::handle_cleanup,
			self, asio::placeholders::error));
		setup_timeout(data_.write_timeout());
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::write_http_error(http_error const &http) {

	data_.log()->error("http error occured with connection[%lu] from %s: %s", ConnectionBase::id(), address(), http.what());
	if (is_policy_ || !ws_info_.empty()) {
		cleanup();
		return;
	}

	try {
		print_error(out_, http);
		std::ostream stream(&out_);
		connection_impl_ptr_type self(this);
		asio::async_write(socket_, out_, boost::bind(&type::handle_cleanup,
			self, asio::placeholders::error));
		setup_timeout(data_.write_timeout());
	}
	catch (std::exception const &e) {
		handle_exception(e);
	}
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::cleanup() {
	timer_.cancel();
	if (!ConnectionBase::name().empty()) {
		boost::intrusive_ptr<ConnectionBase> self(this);
		ct_.manager().remove_connection(self);
	}
	socket_.close();
	data_.log()->info("connection[%lu] from %s is closed", ConnectionBase::id(), address());
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::setup_timeout(unsigned short timeout) {

	connection_impl_ptr_type self(this);
	timer_.expires_from_now(boost::posix_time::milliseconds(timeout));
	timer_.async_wait(boost::bind(&type::handle_timeout, self,
		asio::placeholders::error));
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::setup_inactive_timeout() {

	connection_impl_ptr_type self(this);
	timer_.expires_from_now(boost::posix_time::milliseconds(data_.inactive_timeout()));
	timer_.async_wait(boost::bind(&type::handle_inactive_timeout, self,
		asio::placeholders::error));
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_error(syst::error_code const &code) {
	data_.log()->error("error occured with connection[%lu] from %s: %s", ConnectionBase::id(), address(), code.message().c_str());
	cleanup();
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::handle_exception(std::exception const &exc) {
	data_.log()->error("exception caught with connection[%lu] from %s: %s", ConnectionBase::id(), address(), exc.what());
	cleanup();
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::print_error(std::streambuf &buf, http_error const &http) const {

	std::ostream stream(&buf);
	stream << http_status(http.code());
	stream << http_date(boost::posix_time::second_clock::universal_time());
	stream << http_header::server();
	stream << http_header::connection_close();
	stream << http_header("Content-Type", "text/plain; charset=utf-8");
	stream << http_constants<char>::endl;
}

template <typename ConnectionBase, typename ConnectionTraits> void
connection_impl<ConnectionBase, ConnectionTraits>::print_headers(std::string const &content_type, std::streambuf &buf) const {

	std::ostream stream(&buf);
	if (!ws_info_.empty()) {
		stream << ws_info_;
	}
	else {
		stream << http_status(200);
		stream << http_header::connection_close();
		stream << http_header("Transfer-Encoding", "chunked");
	}
	stream << http_date(boost::posix_time::second_clock::universal_time());
	stream << http_header::server();
	stream << http_header("Content-Type", content_type.c_str());
	stream << http_constants<char>::endl;
}

}} // namespaces

#endif // XIVA_DETAILS_CONNECTION_IMPL_HPP_INCLUDED
