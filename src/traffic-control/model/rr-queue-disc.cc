#include "rr-queue-disc.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/log.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("RRQueueDisc");
NS_OBJECT_ENSURE_REGISTERED(RRQueueDisc);

TypeId
RRQueueDisc::GetTypeId()
{
  static TypeId tid =
    TypeId("ns3::RRQueueDisc")
      .SetParent<QueueDisc>()
      .SetGroupName("TrafficControl")
      .AddConstructor<RRQueueDisc>()
      .AddAttribute("Queues",
                    "Number of RR sub-queues",
                    UintegerValue(4),
                    MakeUintegerAccessor(&RRQueueDisc::m_queues),
                    MakeUintegerChecker<uint32_t>(1))
      .AddAttribute("MaxSize",
                    "Maximum size of each internal queue",
                    QueueSizeValue(QueueSize("100p")),
                    MakeQueueSizeAccessor(&QueueDisc::SetMaxSize,
                                          &QueueDisc::GetMaxSize),
                    MakeQueueSizeChecker());
  return tid;
}

RRQueueDisc::RRQueueDisc()
  : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES),
    m_current(0)
{
}

RRQueueDisc::~RRQueueDisc() {}

bool
RRQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    uint32_t q = 0; 
    Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem>(item);
    if (ipv4Item != nullptr) 
    {
        Ipv4Address srcAddr = ipv4Item->GetHeader().GetSource();
        Ipv4Address destAddr = ipv4Item->GetHeader().GetDestination();

        Address macAddr = item->GetAddress();

        // Băm theo IP nguồn để tách biệt hoàn toàn
        q = srcAddr.Get() % m_queues;
        std::cout << "Time: " << Simulator::Now().GetSeconds() 
                  << "mac: " << macAddr
                  << "s | [RR Classifier] Src: " << srcAddr 
                  << " -> Dst: " << destAddr 
                  << " | Assigned to Queue: " << q << std::endl;

    }
    else 
    {
        // Gói tin hệ thống (ARP, AODV control...) cho vào Queue 0
        q = 0; 
    }

    // 4. Đẩy vào hàng đợi tương ứng
    return GetInternalQueue(q)->Enqueue(item);
}



Ptr<QueueDiscItem>
RRQueueDisc::DoDequeue()
{
  for (uint32_t i = 0; i < m_queues; ++i)
  {
    uint32_t q = (m_current + i) % m_queues;
    Ptr<QueueDiscItem> item = GetInternalQueue(q)->Dequeue();
    if (item)
    {
      m_current = (q + 1) % m_queues; // Chỉ tăng khi lấy được gói
      return item;
    }
  }
  return nullptr;
}

Ptr<const QueueDiscItem>
RRQueueDisc::DoPeek()
{
  for (uint32_t i = 0; i < m_queues; ++i)
  {
    uint32_t q = (m_current + i) % m_queues;
    Ptr<const QueueDiscItem> item = GetInternalQueue(q)->Peek();
    if (item)
    {
      return item;
    }
  }
  return nullptr;
}

bool
RRQueueDisc::CheckConfig()
{
  if (GetNInternalQueues() == 0)
  {
    for (uint32_t i = 0; i < m_queues; ++i)
    {
      Ptr<DropTailQueue<QueueDiscItem>> q =
        CreateObject<DropTailQueue<QueueDiscItem>>();
      q->SetMaxSize(GetMaxSize());
      AddInternalQueue(q);
    }
  }

  if (GetNInternalQueues() != m_queues)
  {
    NS_LOG_ERROR("RRQueueDisc: wrong number of internal queues");
    return false;
  }

  return true;
}

void
RRQueueDisc::InitializeParams()
{
  m_current = 0;
}

} // namespace ns3







