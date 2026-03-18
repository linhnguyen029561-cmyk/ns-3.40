/*
 * Copyright (c) 2022 Universita' degli Studi di Napoli Federico II
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
 *
 * Author: Stefano Avallone <stavallo@unina.it>
 */

#include "fcfs-wifi-queue-scheduler.h"

#include "wifi-mac-queue.h"

#include "ns3/enum.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FcfsWifiQueueScheduler");

bool
operator==(const FcfsPrio& lhs, const FcfsPrio& rhs)
{
    return lhs.token == rhs.token && lhs.type == rhs.type;
}

bool
operator<(const FcfsPrio& lhs, const FcfsPrio& rhs)
{
    // Control queues have the highest priority
    if (lhs.type == WIFI_CTL_QUEUE && rhs.type != WIFI_CTL_QUEUE)
    {
        return true;
    }
    if (lhs.type != WIFI_CTL_QUEUE && rhs.type == WIFI_CTL_QUEUE)
    {
        return false;
    }
    // Management queues have the second highest priority
    if (lhs.type == WIFI_MGT_QUEUE && rhs.type != WIFI_MGT_QUEUE)
    {
        return true;
    }
    if (lhs.type != WIFI_MGT_QUEUE && rhs.type == WIFI_MGT_QUEUE)
    {
        return false;
    }
    // we get here if both priority values refer to container queues of the same type,
    // hence we can compare the tokens.
    return lhs.token < rhs.token;
}

NS_OBJECT_ENSURE_REGISTERED(FcfsWifiQueueScheduler);

TypeId
FcfsWifiQueueScheduler::GetTypeId()
{
    static TypeId tid = TypeId("ns3::FcfsWifiQueueScheduler")
                            .SetParent<WifiMacQueueSchedulerImpl<FcfsPrio>>()
                            .SetGroupName("Wifi")
                            .AddConstructor<FcfsWifiQueueScheduler>()
                            .AddAttribute("DropPolicy",
                                          "Upon enqueue with full queue, drop oldest (DropOldest) "
                                          "or newest (DropNewest) packet",
                                          EnumValue(DROP_NEWEST),
                                          MakeEnumAccessor(&FcfsWifiQueueScheduler::m_dropPolicy),
                                          MakeEnumChecker(FcfsWifiQueueScheduler::DROP_OLDEST,
                                                          "DropOldest",
                                                          FcfsWifiQueueScheduler::DROP_NEWEST,
                                                          "DropNewest"));
    return tid;
}

FcfsWifiQueueScheduler::FcfsWifiQueueScheduler()
    : m_token(0),
      NS_LOG_TEMPLATE_DEFINE("FcfsWifiQueueScheduler")
{
}

Ptr<WifiMpdu>
FcfsWifiQueueScheduler::HasToDropBeforeEnqueuePriv(AcIndex ac, Ptr<WifiMpdu> mpdu)
{
    auto queue = GetWifiMacQueue(ac);
    if (queue->QueueBase::GetNPackets() < queue->GetMaxSize().GetValue())
    {
        // the queue is not full, do not drop anything
        return nullptr;
    }

    // Control and management frames should be prioritized
    if (m_dropPolicy == DROP_OLDEST || mpdu->GetHeader().IsCtl() || mpdu->GetHeader().IsMgt())
    {
        for (const auto& [priority, queueInfo] : GetSortedQueues(ac))
        {
            if (std::get<WifiContainerQueueType>(queueInfo.get().first) == WIFI_MGT_QUEUE ||
                std::get<WifiContainerQueueType>(queueInfo.get().first) == WIFI_CTL_QUEUE)
            {
                // do not drop control or management frames
                continue;
            }

            // do not drop frames that are inflight or to be retransmitted
            Ptr<WifiMpdu> item;
            while ((item = queue->PeekByQueueId(queueInfo.get().first, item)))
            {
                if (!item->IsInFlight() && !item->GetHeader().IsRetry())
                {
                    NS_LOG_DEBUG("Dropping " << *item);
                    return item;
                }
            }
        }
    }
    NS_LOG_DEBUG("Dropping received MPDU: " << *mpdu);
    return mpdu;
}

void
FcfsWifiQueueScheduler::DoNotifyEnqueue(AcIndex ac, Ptr<WifiMpdu> mpdu)
{
    NS_LOG_FUNCTION(this << +ac << *mpdu);

    const auto queueId = WifiMacQueueContainer::GetQueueId(mpdu);

    if (m_queueTokens.find(queueId) == m_queueTokens.end())
    {
        m_queueTokens[queueId] = ++m_token;
    }

    SetPriority(ac, queueId, {m_queueTokens[queueId], std::get<WifiContainerQueueType>(queueId)});
}

void
FcfsWifiQueueScheduler::DoNotifyDequeue(AcIndex ac, const std::list<Ptr<WifiMpdu>>& mpdus)
{
    NS_LOG_FUNCTION(this << +ac << mpdus.size());

    std::set<WifiContainerQueueId> queueIds;

    for (const auto& mpdu : mpdus)
    {
        queueIds.insert(WifiMacQueueContainer::GetQueueId(mpdu));
    }

    for (const auto& queueId : queueIds)
    {
        if (auto item = GetWifiMacQueue(ac)->PeekByQueueId(queueId))
        {
            m_queueTokens[queueId] = ++m_token;
            SetPriority(ac,
                        queueId,
                        {m_queueTokens[queueId], std::get<WifiContainerQueueType>(queueId)});
        }
        else
        {
            // ==== PCRQ ALGORITHM 2: Controlling the Turn of Reading Queues ====
            if (GetWifiMacQueue(ac)->GetEnablePcrq()) {
                uint32_t totalPackets = 0;
                uint32_t numActiveQueues = 0;
                uint32_t qmax = 0;
                
                for (const auto& kv : GetSortedQueues(ac)) {
                    uint32_t qSize = GetWifiMacQueue(ac)->GetNPackets(kv.second.get().first);
                    if (qSize > 0) {
                        totalPackets += qSize;
                        numActiveQueues++;
                        if (qSize > qmax) {
                            qmax = qSize;
                        }
                    }
                }
                
                bool holdTurn = false;
                if (numActiveQueues > 0 && totalPackets > 0) {
                    double ave = static_cast<double>(totalPackets) / numActiveQueues;
                    double beta = GetWifiMacQueue(ac)->GetBeta();
                    // P_turn calculation based on Algorithm 2
                    double p_turn = beta * qmax / (numActiveQueues * ave);
                    
                    static Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
                    double randVal = uv->GetValue();
                    
                    if (randVal <= p_turn) {
                        holdTurn = true;
                        NS_LOG_DEBUG("PCRQ Algorithm 2 Hold Turn: rand=" << randVal << " <= P_turn=" << p_turn 
                                     << " (qmax=" << qmax << ", ave=" << ave << ")");
                    }
                }

                if (holdTurn) {
                    // Keep the queue's token so it retains its turn (or put it to back of line)
                    m_queueTokens[queueId] = ++m_token; 
                    // We do not call SetPriority here because the queue is empty, and ns-3 
                    // requires removing empty queues from the sorted list to avoid infinite loops
                    // when searching for packets. We just keep the token in m_queueTokens.
                } else {
                    m_queueTokens.erase(queueId);
                }
            } else {
                m_queueTokens.erase(queueId);
            }
            // ==== END PCRQ ALGORITHM 2 ====
        }
    }
}

void
FcfsWifiQueueScheduler::DoNotifyRemove(AcIndex ac, const std::list<Ptr<WifiMpdu>>& mpdus)
{
    NS_LOG_FUNCTION(this << +ac << mpdus.size());

    std::set<WifiContainerQueueId> queueIds;

    for (const auto& mpdu : mpdus)
    {
        queueIds.insert(WifiMacQueueContainer::GetQueueId(mpdu));
    }

    for (const auto& queueId : queueIds)
    {
        if (auto item = GetWifiMacQueue(ac)->PeekByQueueId(queueId))
        {
            SetPriority(ac,
                        queueId,
                        {m_queueTokens[queueId], std::get<WifiContainerQueueType>(queueId)});
        }
        else
        {
            // ==== PCRQ ALGORITHM 2: Controlling the Turn of Reading Queues ====
            if (GetWifiMacQueue(ac)->GetEnablePcrq()) {
                uint32_t totalPackets = 0;
                uint32_t numActiveQueues = 0;
                uint32_t qmax = 0;
                
                for (const auto& kv : GetSortedQueues(ac)) {
                    uint32_t qSize = GetWifiMacQueue(ac)->GetNPackets(kv.second.get().first);
                    if (qSize > 0) {
                        totalPackets += qSize;
                        numActiveQueues++;
                        if (qSize > qmax) {
                            qmax = qSize;
                        }
                    }
                }
                
                bool holdTurn = false;
                if (numActiveQueues > 0 && totalPackets > 0) {
                    double ave = static_cast<double>(totalPackets) / numActiveQueues;
                    double beta = GetWifiMacQueue(ac)->GetBeta();
                    // P_turn calculation based on Algorithm 2
                    double p_turn = beta * qmax / (numActiveQueues * ave);
                    
                    static Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
                    double randVal = uv->GetValue();
                    
                    if (randVal <= p_turn) {
                        holdTurn = true;
                        NS_LOG_DEBUG("PCRQ Algorithm 2 Hold Turn: rand=" << randVal << " <= P_turn=" << p_turn 
                                     << " (qmax=" << qmax << ", ave=" << ave << ")");
                    }
                }

                if (holdTurn) {
                    // Keep the queue's token so it retains its turn (or put it to back of line)
                    m_queueTokens[queueId] = ++m_token; 
                } else {
                    m_queueTokens.erase(queueId);
                }
            } else {
                m_queueTokens.erase(queueId);
            }
            // ==== END PCRQ ALGORITHM 2 ====
        }
    }
}

} // namespace ns3

