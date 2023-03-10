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

#include <cstdio>
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <iomanip> 

//#include <format>
#include <iostream>
#include <string>
#include <string_view>

#include <curses.h>
#include <csignal>

#include "utils.h"
#include "cap_mgr.h"
#include "proc.h"
#include "name_res.h"
#include "settings.h"

namespace {
	volatile bool quit = false;
	volatile bool pause = false;

	void sign_onexit(int param) {
		quit = true;
	}

	// 
	const char*	__version__ = "0.3";

	// TODO describe
	struct ps_sorted_iter {
		nettop::ps_vec::const_iterator it_p_vec;
		std::vector<nettop::proc_stats::addr_st_map::const_iterator> v_it_addr;

		// simple copy constructor
		ps_sorted_iter(nettop::ps_vec::const_iterator it_p_vec_) : it_p_vec(it_p_vec_) {}
	};

	typedef std::vector<std::shared_ptr<ps_sorted_iter>> sorted_p_vec;

	void sort_filter_data(const nettop::ps_vec& p_vec, sorted_p_vec& out) {
		// copy the iterators into output vector
		out.resize(0);
		out.reserve(p_vec.size());
		for(nettop::ps_vec::const_iterator it = p_vec.begin(); it != p_vec.end(); ++it) {
			std::shared_ptr<ps_sorted_iter>	el(new ps_sorted_iter(it));
			el->v_it_addr.reserve(it->addr_rs_map.size());
			for(nettop::proc_stats::addr_st_map::const_iterator it_m = it->addr_rs_map.begin(); it_m != it->addr_rs_map.end(); ++it_m)
				el->v_it_addr.push_back(it_m);
			out.push_back(el);
		}

		// filter data if needed
		if(nettop::settings::FILTER_ZERO) {
			sorted_p_vec::iterator	it_erase = std::remove_if(out.begin(), out.end(), [](const std::shared_ptr<ps_sorted_iter>& ps){ return (ps->it_p_vec->total_rs.first + ps->it_p_vec->total_rs.second) == 0; });
			out.erase(it_erase, out.end());
		}

		// sort it (external)
		struct sort_fctr {
			bool operator()(const std::shared_ptr<ps_sorted_iter>& lhs, const std::shared_ptr<ps_sorted_iter>& rhs) {
				const size_t	lhs_sz = lhs->it_p_vec->total_rs.first + lhs->it_p_vec->total_rs.second,
						rhs_sz = rhs->it_p_vec->total_rs.first + rhs->it_p_vec->total_rs.second;
				return (nettop::settings::ORDER_TOP) ? lhs_sz > rhs_sz : lhs_sz < rhs_sz;
			}
		};

		std::sort(out.begin(), out.end(), sort_fctr());

		// sort it (internal)
		struct sort_fctr_int {
			bool operator()(const nettop::proc_stats::addr_st_map::const_iterator& lhs, const nettop::proc_stats::addr_st_map::const_iterator& rhs) {
				const size_t	lhs_sz = lhs->second.recv + lhs->second.sent,
						rhs_sz = rhs->second.recv + rhs->second.sent;
				return (nettop::settings::ORDER_TOP) ? lhs_sz > rhs_sz : lhs_sz < rhs_sz;
			}
		};

		for(auto& i : out) {
			std::sort(i->v_it_addr.begin(), i->v_it_addr.end(), sort_fctr_int());
		}
	}

	class setup {
	protected:
		nettop::name_res& nr;
		constexpr static char BPS[] = "Bytes/s", 
					 	     KBPS[] = "KiB/s", 
						     MBPS[] = "MiB/s", 
						     GBPS[] = "GiB/s";

	public:
		setup(nettop::name_res &nr) : nr(nr) {} 
		
		static void recv_send_format(const std::chrono::nanoseconds& tm_elapsed, const size_t recv, const size_t sent, double& recv_d, double& sent_d, const char* & fmt) {
			const size_t	max_bytes = (recv > sent) ? recv : sent;
			const double	tm_fct = 1000000000.0/tm_elapsed.count();
			
			if(max_bytes >= 1024*1024*1024) {
				const double	cnv_fct = 1.0/(1024.0*1024.0*1024.0);
				recv_d = cnv_fct*recv*tm_fct;
				sent_d = cnv_fct*sent*tm_fct;
				fmt = GBPS;
			} else if(max_bytes >= 1024*1024) {
				const double	cnv_fct = 1.0/(1024.0*1024.0);
				recv_d = cnv_fct*recv*tm_fct;
				sent_d = cnv_fct*sent*tm_fct;
				fmt = MBPS;
			} else if(max_bytes >= 1024) {
				const double	cnv_fct = 1.0/1024.0;
				recv_d = cnv_fct*recv*tm_fct;
				sent_d = cnv_fct*sent*tm_fct;
				fmt = KBPS;
			} else {
				recv_d = tm_fct*recv;
				sent_d = tm_fct*sent;
				fmt = BPS;
			}
		}
		
		void redraw(const std::chrono::nanoseconds& tm_elapsed, 
				const sorted_p_vec& s_v, 
				const size_t total_pkts, 
				const nettop::proc_mgr::stats& st);
	};

	class curses_setup : setup {
		WINDOW *w_;
	
	public:
		curses_setup(nettop::name_res& nr) : 
			setup(nr), w_(initscr()) {}
		
		~curses_setup() { endwin(); }
	
		void redraw(const std::chrono::nanoseconds& tm_elapsed, 
				const sorted_p_vec& s_v, 
				const size_t total_pkts, 
				const nettop::proc_mgr::stats& st) {
			clear();
			int 		row = 0; // number of terminal rows
        	int 		col = 0; // number of terminal columns
        	getmaxyx(stdscr, row, col);      /* find the boundaries of the screeen */
			
			// UI coordinates:
			// 6     2 23                     2 9        2 9        2 5
			// PIDXXX  cmdlineXXXXXXXXXXXXXXXX  recvXXXXX  sentXXXXX  KiB/s
			if(col < 60 || row < 5) {
				mvprintw(0, 0, "Need at least a screen of 60x5 (%d/%d)", col, row);
				refresh();
				return;
			}
			
			const int cmdline_len = col - (6+2+9+2+9+2+6+3);
			int	cur_row = 2;
			size_t tot_recv = 0, tot_sent = 0;

			// print header
			attron(A_REVERSE);
			mvprintw(cur_row++, 0, "%-6s  %-*s  %-9s  %-9s        ", "PID", cmdline_len, "CMDLINE", "RECV", "SENT");
			attroff(A_REVERSE);

			// print each entity
			for(const auto& sp_i : s_v) {
				// print each process row
				const auto&	i = *(sp_i->it_p_vec);
				std::string	r_cmd = i.cmd; 
				double r_d = 0.0, s_d = 0.0;
				
				r_cmd.resize(cmdline_len);

				const char*	fmt = "";

				recv_send_format(tm_elapsed, i.total_rs.first, i.total_rs.second, r_d, s_d, fmt);

				tot_recv += i.total_rs.first;
				tot_sent += i.total_rs.second;

				// if we don't have more UI space, don't bother printing this row..
				if(cur_row >= row-1)
					continue;
				mvprintw(cur_row++, 0, "%6d  %-*s %10.2f %10.2f  %-5s", i.pid, cmdline_len, r_cmd.c_str(), r_d, s_d, fmt);

				// print each server txn
				for(const auto& sp_j : sp_i->v_it_addr) {
					const auto&	j = *sp_j;
					const int host_line = cmdline_len-3;
					const size_t tot_t = j.second.udp_t + j.second.tcp_t,
							udp_p = (tot_t) ? 100.0*j.second.udp_t/(j.second.udp_t + j.second.tcp_t) : 0,
							tcp_p = (tot_t) ? 100 - udp_p : 0;
					char	tcp_udp_buf[32] = "[ na/ na] ";

					if(tot_t) {
						std::snprintf(tcp_udp_buf, 32, "[%3lu/%3lu] ", tcp_p, udp_p);
					}
					
					
					char buf[256];
					std::snprintf(buf, 256, "%s%s", (nettop::settings::TCP_UDP_TRAFFIC) ? tcp_udp_buf : "", nr.to_str(j.first).c_str());
					std::string	r_host = buf; r_host.resize(host_line);
					recv_send_format(tm_elapsed, j.second.recv, j.second.sent, r_d, s_d, fmt);
					mvprintw(cur_row++, 0, "           %-*s %10.2f %10.2f  %-5s", host_line, r_host.c_str(), r_d, s_d, fmt);
				}
			}

			// print the totals and header
			double r_d = 0.0, s_d = 0.0;
			const char*	fmt = "";
			recv_send_format(tm_elapsed, tot_recv, tot_sent, r_d, s_d, fmt);
			char total_buf[128];
			snprintf(total_buf, 128, "%s [%5.2fs (%3lu/%3lu/%3lu)]", __version__, 1.0*tm_elapsed.count()/1000000000.0, st.undet_pkts, st.unmap_r_pkts, st.unmap_s_pkts);
			mvprintw(0, 0, "nettop %-*s", cmdline_len-6, total_buf);
			mvprintw(0, cmdline_len+1, "  Total %10.2f %10.2f  %-5s", r_d, s_d, fmt);
			refresh();
		}
	};

	class json_setup : setup {
	public:
		json_setup(nettop::name_res& nr) : setup(nr) {};
		~json_setup() {};

		void redraw(const std::chrono::nanoseconds& tm_elapsed, 
				const sorted_p_vec& s_v, 
				const size_t total_pkts, 
				const nettop::proc_mgr::stats& st) {
			const int cmdline_len = 80; 
			size_t tot_recv = 0, tot_sent = 0, p_vec_ctr = 0;

			// print header
			//std::cout << "PID" << "CMDLINE" << "RECV" << "SENT" << std::endl;
			std::cout << "[\n";
			
			// print each entity
			for(const auto& sp_i : s_v) {
				// print each process row
				const auto&	i = *(sp_i->it_p_vec);
				std::string	r_cmd = i.cmd; 
				
				if (true) {
					size_t pos = r_cmd.find(" ");
					if (pos !=std::string::npos) {
						r_cmd.resize(pos);
					}
				}

				double r_d = 0.0, s_d = 0.0;
				const char*	fmt = "";

				recv_send_format(tm_elapsed, i.total_rs.first, i.total_rs.second, r_d, s_d, fmt);

				tot_recv += i.total_rs.first;
				tot_sent += i.total_rs.second;
				
				std::cout << "{\n";
				std::cout << "\t\"pid\":" << i.pid << ",\n";
				std::cout << "\t\"cmd\":\"" << r_cmd << "\",\n";
				std::cout << "\t\"fmt\":\"" << fmt << "\",\n";
				std::cout << "\t\"total_recv\":" << r_d << ",\n";
				std::cout << "\t\"total_sent\":" << s_d << ",\n";
				std::cout << "\t\"hosts\": [";	

				// std::printf("%6d %.*s %10.2f %10.2f %-5s\n", i.pid, cmdline_len, r_cmd.c_str(), r_d, s_d, fmt);
				// std::cout << i.pid << " " << std::setw(30) << r_cmd.c_str() << " " << r_d << " " << " " << s_d << fmt << std::endl;

				long int server_txn_ctr = 0, server_txn_len = 0 ;
				
				// this is stupid
				const auto cp_it =  sp_i->v_it_addr;
				for (const auto &i : cp_it) {
					server_txn_len += 1;
					// =-=
					(void)i;
				}
				
				// This oly make sure that if we have a host connection, these
				// are printed into a new line
				if (server_txn_len != 0) {
					std::cout << "\n";
				}
				
				// print each server txn
				for(const auto& sp_j : sp_i->v_it_addr) {
					const auto&	j = *sp_j;
					const int  host_line = cmdline_len-3;
					const size_t tot_t = j.second.udp_t + j.second.tcp_t,
								 udp_p = (tot_t) ? 100.0*j.second.udp_t/(j.second.udp_t + j.second.tcp_t) : 0,
								 tcp_p = (tot_t) ? 100 - udp_p : 0;
					char tcp_udp_buf[32] = "[ na/ na] ";

					if(tot_t) {
						std::snprintf(tcp_udp_buf, 32, "[%3lu/%3lu] ", tcp_p, udp_p);
					}
					
					char buf[256];
					std::snprintf(buf, 256, "%s%s", (nettop::settings::TCP_UDP_TRAFFIC) ? tcp_udp_buf : "", nr.to_str(j.first).c_str());
					std::string	r_host = buf;
					r_host.resize(host_line);
					
					recv_send_format(tm_elapsed, j.second.recv, j.second.sent, r_d, s_d, fmt);
					
					std::cout << "\t\t{\n";
					std::cout << "\t\t\"host\":\"" << r_host << "\",\n";
					std::cout << "\t\t\"fmt\":\""  << fmt << "\",\n";
					std::cout << "\t\t\"recv\":" << r_d << ",\n";
					std::cout << "\t\t\"send\":" << s_d << "\n";

					if (server_txn_ctr != (server_txn_len - 1))
						std::cout << "\t\t},\n";
					else
						std::cout << "\t\t}\n";

					server_txn_ctr += 1;
					//printf("           %.*s %10.2f %10.2f  %.5s\n", host_line, r_host.c_str(), r_d, s_d, fmt);
					// std::cout << "\t" << std::setw(30) << r_host.c_str() << r_d << s_d << fmt << std::endl;
				}

				// print closing bracket for host
				if (server_txn_len != 0)
					std::cout << "\t]\n";
				else
					std::cout << "]\n";

				// print the closing bracket for the whole application
				if (p_vec_ctr != s_v.size() - 1) 
					std::cout << "},\n";
				else
					std::cout << "}\n";

				p_vec_ctr += 1;
			}
			
			// print the totals and header
			double r_d = 0.0, s_d = 0.0;
			const char*	fmt = "";
			recv_send_format(tm_elapsed, tot_recv, tot_sent, r_d, s_d, fmt);
			char	total_buf[128];
			snprintf(total_buf, 128, "%s [%5.2fs (%3lu/%3lu/%3lu)]\n", __version__, 1.0*tm_elapsed.count()/1000000000.0, st.undet_pkts, st.unmap_r_pkts, st.unmap_s_pkts);
			
			//printf("nettop %s", total_buf);
			//printf("Total %10.2f %10.2f  %.5s\n", r_d, s_d, fmt);
			//std::cout << "nettop " << total_buf << std::endl;
			//std::cout << "Total " << r_d << " " << s_d << fmt << std::endl;
			printf("]\n");
		}
	};

	class cli_setup : setup {
	public:
		cli_setup(nettop::name_res& nr) : setup(nr) {};
		~cli_setup() {};

		void redraw(const std::chrono::nanoseconds& tm_elapsed, 
				const sorted_p_vec& s_v, 
				const size_t total_pkts, 
				const nettop::proc_mgr::stats& st) {
			const int cmdline_len = 80; 
			size_t tot_recv = 0, tot_sent = 0;

			// print header
			std::cout << "PID" << "CMDLINE" << "RECV" << "SENT" << std::endl;

			// print each entity
			for(const auto& sp_i : s_v) {
				// print each process row
				const auto&	i = *(sp_i->it_p_vec);
				std::string	r_cmd = i.cmd; 
				
				if (true) {
					size_t pos = r_cmd.find(" ");
					if (pos !=std::string::npos) {
						r_cmd.resize(pos);
					}
				}

				double r_d = 0.0, s_d = 0.0;
				const char*	fmt = "";

				recv_send_format(tm_elapsed, i.total_rs.first, i.total_rs.second, r_d, s_d, fmt);

				tot_recv += i.total_rs.first;
				tot_sent += i.total_rs.second;

				std::printf("%6d %.*s %10.2f %10.2f %-5s\n", i.pid, cmdline_len, r_cmd.c_str(), r_d, s_d, fmt);
				// std::cout << i.pid << " " << std::setw(30) << r_cmd.c_str() << " " << r_d << " " << " " << s_d << fmt << std::endl;

				// print each server txn
				for(const auto& sp_j : sp_i->v_it_addr) {
					const auto&	j = *sp_j;
					const int  host_line = cmdline_len-3;
					const size_t tot_t = j.second.udp_t + j.second.tcp_t,
								 udp_p = (tot_t) ? 100.0*j.second.udp_t/(j.second.udp_t + j.second.tcp_t) : 0,
								 tcp_p = (tot_t) ? 100 - udp_p : 0;
					char	tcp_udp_buf[32] = "[ na/ na] ";

					if(tot_t) {
						std::snprintf(tcp_udp_buf, 32, "[%3lu/%3lu] ", tcp_p, udp_p);
					}
					
					char buf[256];
					std::snprintf(buf, 256, "%s%s", (nettop::settings::TCP_UDP_TRAFFIC) ? tcp_udp_buf : "", nr.to_str(j.first).c_str());
					std::string	r_host = buf;
					r_host.resize(host_line);
					
					recv_send_format(tm_elapsed, j.second.recv, j.second.sent, r_d, s_d, fmt);
					printf("           %.*s %10.2f %10.2f  %.5s\n", host_line, r_host.c_str(), r_d, s_d, fmt);
					// std::cout << "\t" << std::setw(30) << r_host.c_str() << r_d << s_d << fmt << std::endl;
				}
			}

			// print the totals and header
			double r_d = 0.0, s_d = 0.0;
			const char*	fmt = "";
			recv_send_format(tm_elapsed, tot_recv, tot_sent, r_d, s_d, fmt);
			char	total_buf[128];
			snprintf(total_buf, 128, "%s [%5.2fs (%3lu/%3lu/%3lu)]\n", __version__, 1.0*tm_elapsed.count()/1000000000.0, st.undet_pkts, st.unmap_r_pkts, st.unmap_s_pkts);
			
			//printf("nettop %s", total_buf);
			//printf("Total %10.2f %10.2f  %.5s\n", r_d, s_d, fmt);
			std::cout << "nettop " << total_buf << std::endl;
			std::cout << "Total " << r_d << " " << s_d << fmt << std::endl;
			printf("\n");
		}
	};
}

int main(int argc, char *argv[]) {
	try {
		using namespace std::chrono;

		// setup signal functions
		std::signal(SIGINT, sign_onexit);
		std::signal(SIGTERM, sign_onexit);

		// parse settings and params
		nettop::parse_args(argc, argv, argv[0], __version__);

		nettop::packet_list	p_list;
		nettop::cap_mgr	c;
		nettop::local_addr_mgr lam;
		nettop::async_log_list log_list;
		nettop::name_res nr(quit);
		nettop::async_log al(quit, nr, nettop::settings::ASYNC_LOG_FILE, log_list);
		
		// create cap thread
		std::thread	cap_th(&nettop::cap_mgr::async_cap, &c, std::ref(p_list), std::ref(quit));
		cap_th.detach();
		
		// init times
		system_clock::time_point latest_time = std::chrono::system_clock::now();
		
		for (size_t i = 0; (i < nettop::settings::COUNTS) && (!quit); i++) {
			// initialize all required structures and the processes too
			nettop::proc_mgr p_mgr;
			nettop::proc_mgr::stats	mgr_st;
			nettop::ps_vec p_vec;

			// wait for some time
			if(nettop::settings::INIT_SLEEP_TIME) {
				nettop::settings::INIT_SLEEP_TIME = false;
			} else {
				size_t	total_msec_slept = 0;
				while(!quit && !nettop::settings::INIT_SLEEP_TIME) {
					const size_t sleep_interval = 250;
					std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
					total_msec_slept += sleep_interval;
					if(nettop::settings::REFRESH_SECS <= total_msec_slept/1000)
						break;
				}
			}

			// get new packets
			std::list<nettop::packet_stats>	ps_list;
			p_list.swap(ps_list);
			
			// bind to known processes
			const system_clock::time_point cur_time = std::chrono::system_clock::now();
			p_mgr.bind_packets(ps_list, lam, p_vec, mgr_st, log_list);
			
			// sort
			sorted_p_vec s_v;
			sort_filter_data(p_vec, s_v);
			

			// redraw now
			if (nettop::settings::CLI) {
				cli_setup c_window(nr);
 				c_window.redraw(cur_time - latest_time, s_v, ps_list.size(), mgr_st);
			} else if (nettop::settings::JSON) {
				json_setup c_window(nr);
				c_window.redraw(cur_time - latest_time, s_v, ps_list.size(), mgr_st);
			} else {
				curses_setup c_window(nr);
				c_window.redraw(cur_time - latest_time, s_v, ps_list.size(), mgr_st);
			}
			// set latest time
			latest_time = cur_time;
		}
		
		// TODO join all threads
		exit(0);
	} catch(const std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
	} catch(...) {
		std::cerr << "Unknown exception" << std::endl;
	}
}

