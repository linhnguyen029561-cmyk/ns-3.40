/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
#ifndef RR_QUEUE_DISC_H
#define RR_QUEUE_DISC_H
 
#include "queue-disc.h"
 
namespace ns3
{
 
/**
 * \ingroup traffic-control
 *
 * QueueDisc implementing a Round Robin scheduler over multiple internal queues.
 */
class RRQueueDisc : public QueueDisc
{
public:
	static TypeId GetTypeId();
 
	RRQueueDisc();
	~RRQueueDisc() override;
 
	// Drop reason text
	static constexpr const char* LIMIT_EXCEEDED_DROP =
    	"Queue disc limit exceeded";
 
private:
	// Override QueueDisc core functions
	bool DoEnqueue(Ptr<QueueDiscItem> item) override;
	Ptr<QueueDiscItem> DoDequeue() override;
	Ptr<const QueueDiscItem> DoPeek() override;
	bool CheckConfig() override;
	void InitializeParams() override;
 
	// ---- Variables required for Round Robin logic ----
	uint32_t m_queues;   // Number of internal queues
	uint32_t m_quantum; 	// Number of packets served per queue per round
	uint32_t m_current; 	// Current queue index in RR
    
	uint32_t ClassifyToQueue(Ptr<QueueDiscItem> item);
};
 
} // namespace ns3
 
#endif /* RR_QUEUE_DISC_H */




