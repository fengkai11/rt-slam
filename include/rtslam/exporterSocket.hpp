/**
 * \file exporterAbstract.hpp
 *
 * Header file state exporter on a network socket
 *
 * \date 16/03/2011
 * \author croussil
 *
 * \ingroup rtslam
 */


#ifndef EXPORTER_SOCKET_HPP_
#define EXPORTER_SOCKET_HPP_

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include "kernel/threads.hpp"

#include "rtslam/exporterAbstract.hpp"

namespace jafar {
namespace rtslam {

	
	using boost::asio::ip::tcp;
	typedef boost::shared_ptr<tcp::socket> socket_ptr;
	
	class ExporterSocket: public ExporterAbstract
	{
		protected:
			void connectionTask()
			{
				boost::asio::io_service io_service;
				tcp::acceptor a(io_service, tcp::endpoint(tcp::v4(), port));
				while (true)
				{
					// wait for new connection
					socket_ptr mysock(new tcp::socket(io_service));
					a.accept(*mysock);
					std::cout << "ExporterSocket: new client connected." << std::endl;
					
					boost::unique_lock<boost::mutex> l(mutex_data);
					socks.push_back(mysock);
				}
				
			}
	
			void sendTask()
			{
				bool stop = false;
				while (!stop)
				{
					condition_send.wait(boost::lambda::_1 != 0, false);
					if (condition_send.var < 0) stop = true;
					condition_send.var = 0;
					condition_send.unlock();
					if (stop) break;
					
					boost::unique_lock<boost::mutex> l(mutex_data);
					bool remove_sock = false;
					for(std::vector<socket_ptr>::iterator it = socks.begin(); it != socks.end(); it = (remove_sock ? socks.erase(it) : it+1))
					{
						remove_sock = false;
						try {
							boost::asio::write(**it, boost::asio::buffer(message, message_size*sizeof(double)));
						} catch (std::exception &e)
						{
							std::cout << "client disconnected" << std::endl;
							remove_sock = true;
						}
					}
				}
			}
			
			boost::mutex mutex_data;
			kernel::VariableCondition<int> condition_send;
			
			static const int message_size = 25;
			short port;
			std::vector<socket_ptr> socks;
			double message[message_size];
			
		public:
			ExporterSocket(robot_ptr_t robPtr, short port): ExporterAbstract(robPtr),
				condition_send(0), port(port)
			{
				boost::thread thread_connection(boost::bind(&ExporterSocket::connectionTask, this));
				boost::thread thread_send(boost::bind(&ExporterSocket::sendTask, this));
			}
			
			virtual void exportCurrentState()
			{
				if (mutex_data.try_lock())
				{
					// FIXME works only for inertial, should be fixed with the general state framework
					/*
					(1 double)   time
					(12 double) pos(x,y,z) euler(yaw,pitch,roll)
					vel(vx,vy,vz) avel(vyaw,vpitch,vroll)
					(12 double) variances
					
					robot : p q v ab wb g
					*/
					jblas::vec state(6), stateStd(6);
					robPtr->slamPoseToRobotPose(ublas::subrange(robPtr->mapPtr()->filterPtr->x(),0,7), ublas::subrange(robPtr->mapPtr()->filterPtr->P(), 0,7,0,7), state, stateStd);

					message[0] = robPtr->self_time;
					for(int i = 0; i < 6; ++i) message[i+1] = state(i);
					std::swap(message[3+1], message[5+1]); // convention roll/pitch/yaw to yaw/pitch/roll
					for(int i = 6; i < 9; ++i) message[i+1] = robPtr->mapPtr()->filterPtr->x(i);
					for(int i = 9; i < 12; ++i) message[i+1] = 0.; // TODO get value from MTI, with some "non filtered state" feature
					
					for(int i = 0; i < 6; ++i) message[i+1+12] = sqrt(stateStd(i));
					std::swap(message[3+1+12], message[5+1+12]); // convention roll/pitch/yaw to yaw/pitch/roll
					for(int i = 6; i < 9; ++i) message[i+1+12] = sqrt(robPtr->mapPtr()->filterPtr->P(i,i));
					for(int i = 9; i < 12; ++i) message[i+1+12] = 0.; // TODO get value from MTI, with some "non filtered state" feature
					
					mutex_data.unlock();
					condition_send.setAndNotify(1);
				} else
					std::cout << "ExporterSocket: not finished sending previous message, connect less clients!" << std::endl;
			}
			
			virtual void stop() { condition_send.setAndNotify(-1); }
	};
	

}}


#endif // EXPORTERSOCKET_HPP
