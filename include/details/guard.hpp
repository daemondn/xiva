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

#ifndef XIVA_DETAILS_GUARD_HPP_INCLUDED
#define XIVA_DETAILS_GUARD_HPP_INCLUDED

#include <boost/thread/mutex.hpp>

namespace xiva { namespace details {

class guard {

public:
	guard();
	virtual ~guard();

	bool open();   // once
	bool commit(); // allow close (after open)
	bool cancel(); // disallow close (after open)
	bool close();  // once (after commit)

private:
	guard(guard const &);
	guard& operator = (guard const &);

private:
	boost::mutex mutex_;
	int state_;
};

}} // namespaces

#endif // XIVA_DETAILS_GUARD_HPP_INCLUDED
