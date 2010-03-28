// xiva (acronym for HTTP Extended EVent Automata) is a simple HTTP server.
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

#ifndef XIVA_DETAILS_SERVER_IMPL_HPP_INCLUDED
#define XIVA_DETAILS_SERVER_IMPL_HPP_INCLUDED

#include <vector>
#include <string>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/thread/thread.hpp>

#include "xiva/config.hpp"
#include "xiva/forward.hpp"
#include "xiva/component_set.hpp"
#include "xiva/globals.hpp"

#include "details/asio.hpp"
#include "details/connection_data.hpp"
#include "details/compound_listener.hpp"
#include "details/connection_traits_base.hpp"
#include "details/acceptor_base.hpp"
#include "details/connection_manager_base.hpp"

namespace xiva { namespace details {

class acceptor_base;
class message_queue;
class connection_manager_base;

class server_impl : public component_set, private boost::thread_group {

public:
	server_impl();
	virtual ~server_impl();

	typedef boost::function<globals::provider_type> provider_closure_type;

	void stop();
	void start(settings const &s);

	void send(std::string const &to, boost::shared_ptr<message> const &m);
	void send(globals::connection_id const &to, boost::shared_ptr<message> const &m);

	virtual void attach_logger(boost::intrusive_ptr<logger> const &logger);
	virtual void attach_receiver_matcher(boost::intrusive_ptr<receiver_matcher> const &m);
	virtual void add_connection_listener(boost::intrusive_ptr<connection_listener> const &l);
	virtual void start_provider(unsigned short nthreads, boost::function<globals::provider_type> f);

private:
	server_impl(server_impl const &);
	server_impl& operator = (server_impl const &);

	typedef std::pair<provider_closure_type, unsigned short> thread_param_type;

	void stop_service();
	void start_provider_thread(thread_param_type const &tp);
	void provider_thread_func(boost::function<globals::provider_type> f);

private:
	asio::io_service io_;
	asio::io_service::strand strand_;

	connection_data data_;
	mutable boost::mutex mutex_;
	std::vector<thread_param_type> providers_;

	boost::intrusive_ptr<logger> logger_;
	boost::intrusive_ptr<compound_listener> listener_;
	boost::intrusive_ptr<acceptor_base> acceptor_;
	boost::intrusive_ptr<receiver_matcher> matcher_;
	boost::intrusive_ptr<message_queue> message_queue_;
	boost::intrusive_ptr<connection_manager_base> connection_manager_;
	boost::intrusive_ptr<connection_traits_base> connection_traits_;
};

}} // namespaces

#endif // XIVA_DETAILS_SERVER_IMPL_HPP_INCLUDED
