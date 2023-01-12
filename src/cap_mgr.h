/*
*	nettop (C) 2014 E. Oriani, ema <AT> fastwebnet <DOT> it
*
*	This file is part of nettop.
*
*	nettop is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	nettop is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with nettop.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _CAP_MGR_H_
#define _CAP_MGR_H_

#include <pcap.h>
#include "mt_list.h"
#include "packet_stats.h"

namespace nettop {
	typedef mt_list<packet_stats>	packet_list;

	class cap_mgr {
		cap_mgr(const cap_mgr&) = delete;
		cap_mgr& operator=(const cap_mgr&) = delete;

		pcap_t	*p_;
public:
		cap_mgr();

		~cap_mgr();

		size_t capture_dispatch(packet_list& p_list);

		void async_cap(packet_list& p_list, volatile bool& quit);
	};
}


#endif //_CAP_MGR_H_

