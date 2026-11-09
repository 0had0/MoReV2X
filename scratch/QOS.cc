/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Multi-class 5G NR sidelink Mode 2 scenarios driven by SUMO traces.
 *
 *   QOS --trace=<folder> --flows=<flows.csv> --simTime=20 --ReEvaluation=1 --Preemption=1
 *
 * <folder> is an output of tools/qos/make_traces.py: trace.csv, roles.csv and, for the urban
 * scenario, buildings.csv. <flows.csv> has one traffic class per row (tools/qos/sweep.py writes
 * it from a YAML file, the columns are listed in ReadFlows). Each class gets its own multicast
 * group, hence its own sidelink logical channel and its own Mode 2 process in every UE.
 *
 * Logs written next to the MoReV2X ones in results/<run>/:
 *   AppTxLog.txt   packet,node,flow,pqi,priority,genTime,size,pdb   one row per generated packet
 *   FlowMap.txt    flow index,name,pqi,priority,pdb,per,gbr,scheduling,rri,group L2 id
 *   NodeMap.txt    node,role,group,position,flows (space separated flow indices)
 *   Impairments.txt what was injected
 */

#include <ns3/applications-module.h>
#include <ns3/config-store.h>
#include <ns3/core-module.h>
#include <ns3/internet-module.h>
#include <ns3/mobility-module.h>
#include <ns3/network-module.h>
#include <ns3/nist-lte-helper.h>
#include <ns3/MoReV2X-module.h>
#include "ns3/nist-sl-resource-pool-factory.h"
#include "ns3/nist-sl-preconfig-pool-factory.h"
#include "ns3/buildings-helper.h"
#include "ns3/nr-v2x-propagation-loss-model.h"
#include "ns3/nr-v2x-spectrum-value-helper.h"
#include "ns3/nr-v2x-tag.h"
#include "ns3/nr-v2x-utils.h"
#include "ns3/nr-v2x-ue-net-device.h"
#include "ns3/sumo-mobility-helper.h"
#include "ns3/waveform-generator.h"
#include "ns3/rng-seed-manager.h"
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

NS_LOG_COMPONENT_DEFINE ("QosScenario");

using namespace ns3;

namespace {

uint64_t g_packetId = 0;
std::vector<bool> g_active;  // node index -> a vehicle drives it now
std::string g_outputPath;

struct TxRecord
{
  uint64_t packet;
  uint32_t node;
  uint32_t flow;
  double genTime;
  uint32_t size;
};
std::vector<TxRecord> g_txLog;

std::vector<std::string>
Split (const std::string &text, char separator)
{
  std::vector<std::string> tokens;
  std::stringstream stream (text);
  std::string token;
  while (std::getline (stream, token, separator))
    {
      tokens.push_back (token);
    }
  return tokens;
}

struct FlowSpec
{
  std::string name;
  uint32_t pqi;
  uint8_t priority;     // SL priority 1..8, sent in the SCI
  double pdb;           // ms
  double per;
  bool gbr;
  std::set<std::string> roles;
  double share;         // fraction of the nodes with one of the roles that run the flow
  std::string model;    // periodic | aperiodic | event
  double periodMs;      // periodic: period. aperiodic: constant part of the interval
  double expMeanMs;     // aperiodic: mean of the exponential part
  double eventRate;     // event: events per second per node
  uint32_t burst;       // event: packets per event
  double burstIntervalMs;
  std::vector<uint32_t> sizes;
  std::vector<double> sizeProbabilities;
  uint32_t reservationSize;
  double rriMs;
  bool sps;             // false: dynamic, a new selection for every packet
  double stackDelayMs;  // added between generation and the sidelink, models higher-layer latency
  double stackJitterMs;
  uint32_t retx;        // blind retransmissions of every packet, 0 to 2
  uint16_t port;
  Ipv4Address group;
  uint32_t l2;
};

std::vector<FlowSpec>
ReadFlows (const std::string &path)
{
  // name,pqi,priority,pdb_ms,per,gbr,roles,share,model,period_ms,exp_mean_ms,event_rate,burst,
  // burst_interval_ms,sizes,size_probs,reservation_size,rri_ms,scheduling,stack_delay_ms,stack_jitter_ms,retx
  std::ifstream file (path.c_str ());
  NS_ABORT_MSG_IF (!file.is_open (), "Flows file not found: " << path);
  std::vector<FlowSpec> flows;
  std::string line;
  std::getline (file, line); // header
  while (std::getline (file, line))
    {
      if (line.empty () || line[0] == '#')
        {
          continue;
        }
      std::vector<std::string> f = Split (line, ',');
      NS_ABORT_MSG_IF (f.size () != 22, "Expected 22 columns in " << path << ", got " << f.size () << ": " << line);
      FlowSpec spec;
      spec.name = f[0];
      spec.pqi = std::stoul (f[1]);
      spec.priority = std::stoul (f[2]);
      spec.pdb = std::stod (f[3]);
      spec.per = std::stod (f[4]);
      spec.gbr = std::stoul (f[5]) != 0;
      for (const std::string &role : Split (f[6], '|'))
        {
          spec.roles.insert (role);
        }
      spec.share = std::stod (f[7]);
      spec.model = f[8];
      spec.periodMs = std::stod (f[9]);
      spec.expMeanMs = std::stod (f[10]);
      spec.eventRate = std::stod (f[11]);
      spec.burst = std::stoul (f[12]);
      spec.burstIntervalMs = std::stod (f[13]);
      for (const std::string &size : Split (f[14], '|'))
        {
          spec.sizes.push_back (std::stoul (size));
        }
      for (const std::string &p : Split (f[15], '|'))
        {
          spec.sizeProbabilities.push_back (std::stod (p));
        }
      spec.reservationSize = std::stoul (f[16]);
      spec.rriMs = std::stod (f[17]);
      spec.sps = (f[18] == "sps");
      NS_ABORT_MSG_IF (f[18] != "sps" && f[18] != "dynamic", "scheduling is sps or dynamic, got " << f[18]);
      spec.stackDelayMs = std::stod (f[19]);
      spec.stackJitterMs = std::stod (f[20]);
      spec.retx = std::stoul (f[21]);
      NS_ABORT_MSG_IF (spec.retx > 2, "flow " << f[0] << ": at most 2 blind retransmissions");
      NS_ABORT_MSG_IF (spec.priority < 1 || spec.priority > 8, "priority goes from 1 to 8 in flow " << spec.name);
      NS_ABORT_MSG_IF (spec.pdb > spec.rriMs, "flow " << spec.name << ": the PDB must not exceed the RRI");
      NS_ABORT_MSG_IF (spec.sizes.size () != spec.sizeProbabilities.size (), "flow " << spec.name << ": sizes and size_probs differ in length");
      NS_ABORT_MSG_IF (spec.model != "periodic" && spec.model != "aperiodic" && spec.model != "event",
                       "flow " << spec.name << ": model is periodic, aperiodic or event");
      flows.push_back (spec);
    }
  NS_ABORT_MSG_IF (flows.empty (), "No flow in " << path);
  NS_ABORT_MSG_IF (flows.size () > 8, "At most 8 flows, the RRC has a limited number of sidelink LCIDs");
  return flows;
}

struct NodeRole
{
  std::string role;
  int group;
  int position;
};

std::vector<NodeRole>
ReadRoles (const std::string &path)
{
  std::ifstream file (path.c_str ());
  NS_ABORT_MSG_IF (!file.is_open (), "Roles file not found: " << path);
  std::vector<NodeRole> roles;
  std::string line;
  std::getline (file, line);
  while (std::getline (file, line))
    {
      std::vector<std::string> f = Split (line, ',');
      if (f.size () < 5)
        {
          continue;
        }
      uint32_t index = std::stoul (f[0]);
      if (roles.size () <= index)
        {
          roles.resize (index + 1);
        }
      roles[index] = {f[2], std::stoi (f[3]), std::stoi (f[4])};
    }
  return roles;
}

/**
 * One traffic class on one node. Packets carry the MoReV2X tag the MAC reads: PDB, RRI, sizes,
 * priority, and a reselection counter of 1 for dynamic scheduling.
 */
class QosFlowApp : public Application
{
public:
  static TypeId GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::QosFlowApp").SetParent<Application> ().AddConstructor<QosFlowApp> ();
    return tid;
  }
  void Setup (const FlowSpec *spec, uint32_t flowIndex, uint32_t nodeIndex)
  {
    m_spec = spec;
    m_flow = flowIndex;
    m_node = nodeIndex;
  }

private:
  virtual void StartApplication (void)
  {
    m_uniform = CreateObject<UniformRandomVariable> ();
    m_exponential = CreateObject<ExponentialRandomVariable> ();
    m_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
    m_socket->SetAllowBroadcast (true);
    m_socket->Bind ();
    m_socket->Connect (InetSocketAddress (m_spec->group, m_spec->port));
    double first;
    if (m_spec->model == "event")
      {
        first = NextEventGap ();
      }
    else
      {
        first = m_uniform->GetValue (0, std::max (m_spec->periodMs, 1.0)) / 1000.0;
      }
    m_event = Simulator::Schedule (Seconds (first), &QosFlowApp::Generate, this, m_spec->model == "event" ? m_spec->burst : 0);
  }
  virtual void StopApplication (void)
  {
    Simulator::Cancel (m_event);
    if (m_socket)
      {
        m_socket->Close ();
      }
  }
  double NextEventGap (void)
  {
    return m_spec->eventRate > 0 ? m_exponential->GetValue (1.0 / m_spec->eventRate, 0) : 1e9;
  }
  uint32_t DrawSize (void)
  {
    double u = m_uniform->GetValue (0, 1), sum = 0;
    for (std::size_t i = 0; i < m_spec->sizes.size (); ++i)
      {
        sum += m_spec->sizeProbabilities[i];
        if (u <= sum)
          {
            return m_spec->sizes[i];
          }
      }
    return m_spec->sizes.back ();
  }
  // burstLeft: packets still due in the current event (event model only)
  void Generate (uint32_t burstLeft)
  {
    if (g_active[m_node])
      {
        uint32_t size = DrawSize ();
        NrV2XTag tag;
        tag.SetGenTime (Simulator::Now ().GetSeconds ());
        tag.SetDoubleValue (Simulator::Now ().GetSeconds ());
        tag.SetMessageType (0x00);
        tag.SetTrafficType (m_spec->model == "periodic" ? 0x00 : 0x01);
        tag.SetPPPP (m_spec->priority);
        tag.SetTxPerTb (1 + m_spec->retx);
        tag.SetPrsvp (m_spec->rriMs);
        tag.SetPdb (m_spec->pdb);
        tag.SetNodeId (GetNode ()->GetId ());
        tag.SetReselectionCounter (m_spec->sps ? 10000 : 1); // 1: the grant expires after this packet
        // MoReV2X counts 35 B of UDP/IP/PDCP/RLC overhead on top of the application payload
        tag.SetPacketSize (size + 35);
        tag.SetReservationSize (std::max (size, m_spec->reservationSize) + 35);
        ++g_packetId;
        tag.SetIntValue (g_packetId);
        tag.SetPacketId (g_packetId);
        Vector position = GetNode ()->GetObject<MobilityModel> ()->GetPosition ();
        tag.SetGenPosX (position.x);
        tag.SetGenPosY (position.y);
        tag.SetNumHops (0);
        Ptr<Packet> packet = Create<Packet> (size);
        packet->AddByteTag (tag);
        g_txLog.push_back ({g_packetId, GetNode ()->GetId (), m_flow, Simulator::Now ().GetSeconds (), size});
        double delay = m_spec->stackDelayMs + (m_spec->stackJitterMs > 0 ? m_uniform->GetValue (0, m_spec->stackJitterMs) : 0);
        if (delay > 0)
          {
            Simulator::Schedule (MilliSeconds (delay), &QosFlowApp::Send, this, packet);
          }
        else
          {
            Send (packet);
          }
      }
    double next;
    uint32_t left = 0;
    if (m_spec->model == "periodic")
      {
        next = m_spec->periodMs / 1000.0;
      }
    else if (m_spec->model == "aperiodic")
      {
        next = (m_spec->periodMs + m_exponential->GetValue (m_spec->expMeanMs, 0)) / 1000.0;
      }
    else if (burstLeft > 1)
      {
        next = m_spec->burstIntervalMs / 1000.0;
        left = burstLeft - 1;
      }
    else
      {
        next = NextEventGap ();
        left = m_spec->burst;
      }
    m_event = Simulator::Schedule (Seconds (next), &QosFlowApp::Generate, this, left);
  }
  void Send (Ptr<Packet> packet)
  {
    m_socket->Send (packet);
  }

  const FlowSpec *m_spec = 0;
  uint32_t m_flow = 0;
  uint32_t m_node = 0;
  Ptr<Socket> m_socket;
  EventId m_event;
  Ptr<UniformRandomVariable> m_uniform;
  Ptr<ExponentialRandomVariable> m_exponential;
};

NS_OBJECT_ENSURE_REGISTERED (QosFlowApp);

void
WriteTxLog (const std::vector<FlowSpec> &flows)
{
  std::ofstream out ((g_outputPath + "AppTxLog.txt").c_str ());
  for (const TxRecord &r : g_txLog)
    {
      const FlowSpec &f = flows[r.flow];
      out << r.packet << "," << r.node << "," << r.flow << "," << f.pqi << "," << (int) f.priority << "," << r.genTime
          << "," << r.size << "," << f.pdb << "\n";
    }
}

void
PrintProgress (double step)
{
  std::cout << "t=" << Simulator::Now ().GetSeconds () << " s, packets=" << g_packetId << std::endl;
  Simulator::Schedule (Seconds (step), &PrintProgress, step);
}

} // namespace

int
main (int argc, char *argv[])
{
  std::string traceDir, flowsFile, outputPath = "results/qos/";
  double simTime = 20.0;
  uint32_t seed = 1, runNumber = 1;
  uint32_t mcs = 14;
  uint16_t numerology = 2, channelBW = 20;
  uint32_t subchannelSize = 12;
  double txPower = 23.0, rsrpThreshold = -128.0, sensitivity = -103.5, rssiThreshold = -88.0;
  bool reEvaluation = false, preemption = false, priorityAware = false, urban = false;
  uint32_t preemptionPriority = 0;
  std::string buildings = "";
  double sizeThreshold = 0.2;
  std::string jammer = "";       // x,y,power dBm,duty cycle,period ms,start s,stop s
  std::string burstLoss = "";    // roles|...;mean good s;mean bad s
  double rxTimeResolution = 1e-5;
  double savingPeriod = 0.5;
  bool progress = false;

  CommandLine cmd;
  cmd.AddValue ("trace", "Folder written by tools/qos/make_traces.py", traceDir);
  cmd.AddValue ("flows", "Traffic classes, one per row", flowsFile);
  cmd.AddValue ("out", "Output folder, ends with /", outputPath);
  cmd.AddValue ("simTime", "Simulated seconds, at most the trace duration", simTime);
  cmd.AddValue ("seed", "Random seed", seed);
  cmd.AddValue ("runNo", "Run number", runNumber);
  cmd.AddValue ("mcs", "MCS of every grant", mcs);
  cmd.AddValue ("Numerology", "0..2, 2 is 60 kHz and 0.25 ms slots", numerology);
  cmd.AddValue ("ChannelBW", "MHz", channelBW);
  cmd.AddValue ("SubChannel", "Subchannel size in RBs", subchannelSize);
  cmd.AddValue ("ueTxPower", "dBm", txPower);
  cmd.AddValue ("RSRP", "Base RSRP threshold of step 5, dBm", rsrpThreshold);
  cmd.AddValue ("Sensitivity", "PHY reference sensitivity, dBm", sensitivity);
  cmd.AddValue ("RSSIthreshold", "CBR busy threshold, dBm", rssiThreshold);
  cmd.AddValue ("ReEvaluation", "Re-evaluate resources not yet announced", reEvaluation);
  cmd.AddValue ("Preemption", "Pre-emption check of announced resources", preemption);
  cmd.AddValue ("PreemptionPriority", "0: any higher priority pre-empts, p: only priorities below p", preemptionPriority);
  cmd.AddValue ("PriorityAware", "Priority pair RSRP thresholds and X% per priority", priorityAware);
  cmd.AddValue ("SizeThreshold", "X of step 6 when PriorityAware is off", sizeThreshold);
  cmd.AddValue ("Urban", "TR 37.885 urban channel, buildings from <trace>/buildings.csv", urban);
  cmd.AddValue ("Buildings", "Buildings CSV, default <trace>/buildings.csv when --Urban", buildings);
  cmd.AddValue ("Jammer", "Wideband jammer: x,y,powerDbm,dutyCycle,periodMs,startS,stopS", jammer);
  cmd.AddValue ("BurstLoss", "Gilbert-Elliott receiver bursts: roles|...;meanGoodS;meanBadS", burstLoss);
  cmd.AddValue ("RxTimeResolution", "Time resolution of ReceivedLog.txt, s", rxTimeResolution);
  cmd.AddValue ("SavingPeriod", "Seconds between two flushes of the logs", savingPeriod);
  cmd.AddValue ("progress", "Print the simulated time every second", progress);
  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF (traceDir.empty () || flowsFile.empty (), "--trace and --flows are required");
  NS_ABORT_MSG_IF (numerology > 2, "FR1: numerology 0, 1 or 2");
  if (outputPath.back () != '/')
    {
      outputPath += "/";
    }
  g_outputPath = outputPath;
  system (("rm -rf " + outputPath + " && mkdir -p " + outputPath).c_str ());

  std::vector<FlowSpec> flows = ReadFlows (flowsFile);
  std::vector<NodeRole> roles = ReadRoles (traceDir + "/roles.csv");
  const uint32_t ueCount = roles.size ();
  NS_ABORT_MSG_IF (ueCount == 0, "No node in " << traceDir << "/roles.csv");

  RngSeedManager::SetSeed (seed);
  RngSeedManager::SetRun (runNumber);

  const uint32_t scsFactor = 1u << numerology;
  const uint16_t channelRbs = GetRbsFromBW (15 * scsFactor, channelBW);
  const double slot = 1.0 / scsFactor;
  NS_ABORT_MSG_IF (channelRbs < subchannelSize, "The channel is smaller than one subchannel");

  // Every packet must fit one slot, the MAC drops the others as "oversize"
  {
    Ptr<NrV2XAmc> amc = CreateObject<NrV2XAmc> ();
    uint32_t largest = amc->GetSlMaxTbSize (mcs, subchannelSize, channelRbs);
    std::cout << "slot payload limit: " << largest - 35 << " B (TB of " << largest << " B at MCS " << mcs << " on "
              << channelRbs << " RBs, minus 35 B of headers)" << std::endl;
    for (const FlowSpec &f : flows)
      {
        for (uint32_t size : f.sizes)
          {
            if (size + 35 > largest)
              {
                std::cerr << "warning: flow " << f.name << " has " << size << " B packets, one slot carries "
                          << largest - 35 << " B of payload. They will be dropped as oversize." << std::endl;
              }
          }
      }
  }

  // ---------------------------------------------------------------- MAC
  Config::SetDefault ("ns3::NrV2XUeMac::SlGrantMcs", UintegerValue (mcs));
  Config::SetDefault ("ns3::NrV2XUeMac::ListL2Enabled", BooleanValue (false));
  Config::SetDefault ("ns3::NrV2XUeMac::MixedTraffic", BooleanValue (false));
  Config::SetDefault ("ns3::NrV2XUeMac::DynamicScheduling", BooleanValue (false)); // per packet, from the tag
  Config::SetDefault ("ns3::NrV2XUeMac::AllowReEvaluation", BooleanValue (reEvaluation));
  Config::SetDefault ("ns3::NrV2XUeMac::Preemption", BooleanValue (preemption));
  Config::SetDefault ("ns3::NrV2XUeMac::PreemptionPriority", UintegerValue (preemptionPriority));
  Config::SetDefault ("ns3::NrV2XUeMac::PriorityAwareSensing", BooleanValue (priorityAware));
  Config::SetDefault ("ns3::NrV2XUeMac::RSRPthreshold", DoubleValue (rsrpThreshold));
  Config::SetDefault ("ns3::NrV2XUeMac::EnableReTx", BooleanValue (false));
  Config::SetDefault ("ns3::NrV2XUeMac::SubchannelSize", UintegerValue (subchannelSize));
  Config::SetDefault ("ns3::NrV2XUeMac::RBsBandwidth", UintegerValue (channelRbs));
  Config::SetDefault ("ns3::NrV2XUeMac::SlotDuration", DoubleValue (slot));
  Config::SetDefault ("ns3::NrV2XUeMac::NumerologyIndex", UintegerValue (numerology));

  // ---------------------------------------------------------------- PHY
  Config::SetDefault ("ns3::NrV2XUePhy::TxPower", DoubleValue (txPower));
  Config::SetDefault ("ns3::NistLteUePowerControl::Pcmax", DoubleValue (txPower));
  Config::SetDefault ("ns3::NistLteUePowerControl::PoNominalPusch", IntegerValue (-106));
  Config::SetDefault ("ns3::NistLteUePowerControl::PscchTxPower", DoubleValue (txPower));
  Config::SetDefault ("ns3::NistLteUePowerControl::PsschTxPower", DoubleValue (txPower));
  Config::SetDefault ("ns3::NrV2XUePhy::RsrpUeMeasThreshold", DoubleValue (-10.0));
  Config::SetDefault ("ns3::NrV2XUePhy::ReferenceSensitivity", DoubleValue (sensitivity));
  Config::SetDefault ("ns3::NrV2XUePhy::RSSIthreshold", DoubleValue (rssiThreshold));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::ReferenceSensitivity", DoubleValue (sensitivity));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::CtrlErrorModelEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::CtrlFullDuplexEnabled", BooleanValue (false));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::SaveCollisionLossesUnimore", BooleanValue (true));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::RxTimeResolution", DoubleValue (rxTimeResolution));
  // SUMO coordinates: log everything except the parking lot
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::MetricsMinX", DoubleValue (-5e4));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::MetricsMaxX", DoubleValue (1e7));
  Config::SetDefault ("ns3::NrV2XUePhy::CbrMinX", DoubleValue (-5e4));
  Config::SetDefault ("ns3::NrV2XUePhy::CbrMaxX", DoubleValue (1e7));
  Config::SetDefault ("ns3::NrV2XUeMac::RandomV2VSelection", BooleanValue (false));
  Config::SetDefault ("ns3::NistLteRlcUm::MaxTxBufferSize", StringValue ("100000"));
  // A new packet of a class replaces an older one still waiting: one SDU per TB in MoReV2X
  Config::SetDefault ("ns3::NistLteRlcUm::KeepOnlyNewestSdu", BooleanValue (true));
  Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue (10000000));

  Config::SetDefault ("ns3::NistLteRlcUm::OutputPath", StringValue (outputPath));
  Config::SetDefault ("ns3::NrV2XUeMac::OutputPath", StringValue (outputPath));
  Config::SetDefault ("ns3::NrV2XUePhy::OutputPath", StringValue (outputPath));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::OutputPath", StringValue (outputPath));
  Config::SetDefault ("ns3::NrV2XUeMac::SavingPeriod", DoubleValue (savingPeriod));
  Config::SetDefault ("ns3::NrV2XUePhy::SavingPeriod", DoubleValue (savingPeriod));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::SavingPeriod", DoubleValue (savingPeriod));

  Config::SetDefault ("ns3::NrV2XSpectrumPhy::SubchannelSize", UintegerValue (subchannelSize));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::RBsBandwidth", UintegerValue (channelRbs));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::SlotDuration", DoubleValue (slot));
  const double dataDuration[] = {1e6 - 71350 - 1, 0.5e6 - 35680 - 1, 0.25e6 - 17840 - 1};
  Config::SetDefault ("ns3::NrV2XUePhy::SidelinkDataDuration", TimeValue (NanoSeconds (dataDuration[numerology])));
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::SubCarrierSpacing", UintegerValue (15 * scsFactor));
  Config::SetDefault ("ns3::NrV2XUePhy::SubCarrierSpacing", UintegerValue (15 * scsFactor));
  Config::SetDefault ("ns3::NrV2XUePhy::SlotDuration", DoubleValue (slot));
  Config::SetDefault ("ns3::NrV2XUePhy::SubchannelSize", UintegerValue (subchannelSize));
  Config::SetDefault ("ns3::NrV2XUePhy::RBsBandwidth", UintegerValue (channelRbs));
  Config::SetDefault ("ns3::NrV2XUePhy::IBE", BooleanValue (true));
  Config::SetDefault ("ns3::NistLtePhy::TTI", DoubleValue (1.0 / (1000 * scsFactor)));

  // ---------------------------------------------------------------- channel
  Config::SetDefault ("ns3::NrV2XPropagationLossModel::Frequency", DoubleValue (5.9));
  Config::SetDefault ("ns3::NrV2XPropagationLossModel::Sigma", DoubleValue (3.0));
  Config::SetDefault ("ns3::NrV2XPropagationLossModel::SigmaNLOSv", DoubleValue (4.0));
  // TR 37.885 Table 6.2.1-1: 25 m on the highway, 10 m in the urban grid
  Config::SetDefault ("ns3::NrV2XPropagationLossModel::DecorrDistance", DoubleValue (urban ? 10.0 : 25.0));
  Config::SetDefault ("ns3::NrV2XPropagationLossModel::Urban", BooleanValue (urban));
  if (urban)
    {
      if (buildings.empty ())
        {
          std::ifstream probe ((traceDir + "/buildings.csv").c_str ());
          if (probe.good ())
            {
              buildings = traceDir + "/buildings.csv";
            }
        }
      Config::SetDefault ("ns3::NrV2XPropagationLossModel::BuildingsFile", StringValue (buildings));
    }

  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults ();

  Ptr<NistPointToPointEpcHelper> epcHelper = CreateObject<NistPointToPointEpcHelper> ();
  Ptr<NistLteHelper> lteHelper = CreateObject<NistLteHelper> ();
  lteHelper->SetPathlossModelType ("ns3::NrV2XPropagationLossModel");
  Ptr<NrV2XPropagationLossModel> channelModel = CreateObject<NrV2XPropagationLossModel> ();
  Config::SetDefault ("ns3::NrV2XSpectrumPhy::ChannelMatrix", PointerValue (channelModel));
  lteHelper->SetAttribute ("UseSidelink", BooleanValue (true));
  lteHelper->SetEpcHelper (epcHelper);
  lteHelper->Initialize ();
  Ptr<NistLteProseHelper> proseHelper = CreateObject<NistLteProseHelper> ();
  proseHelper->SetLteHelper (lteHelper);

  // ---------------------------------------------------------------- nodes and mobility
  NodeContainer ues;
  for (uint32_t i = 0; i < ueCount; ++i)
    {
      Ptr<Node> node = CreateObject<Node> ();
      Ptr<LTENodeState> state = CreateObject<LTENodeState> ();
      state->SetNode (node);
      node->AggregateObject (state);
      ues.Add (node);
    }
  SumoMobilityConfig mobilityConfig;
  mobilityConfig.traceCsv = traceDir + "/trace.csv";
  mobilityConfig.altitude = 1.5;
  mobilityConfig.seed = seed;
  SumoMobilityHelper mobility (mobilityConfig);
  mobility.InstallParked (ues);
  g_active.assign (ueCount, false);
  mobility.Start ([&ues] (Ptr<Node> node, const std::string &) { g_active[node->GetId () - ues.Get (0)->GetId ()] = true; },
                  [&ues] (Ptr<Node> node, const std::string &) { g_active[node->GetId () - ues.Get (0)->GetId ()] = false; });

  // The jammer is a node of the channel matrix, so it has a pathloss to every UE
  NodeContainer channelNodes (ues);
  Ptr<Node> jammerNode;
  std::vector<double> jam;
  if (!jammer.empty ())
    {
      for (const std::string &v : Split (jammer, ','))
        {
          jam.push_back (std::stod (v));
        }
      NS_ABORT_MSG_IF (jam.size () != 7, "--Jammer=x,y,powerDbm,dutyCycle,periodMs,startS,stopS");
      jammerNode = CreateObject<Node> ();
      Ptr<ConstantPositionMobilityModel> position = CreateObject<ConstantPositionMobilityModel> ();
      position->SetPosition (Vector (jam[0], jam[1], 1.5));
      jammerNode->AggregateObject (position);
      channelNodes.Add (jammerNode);
    }
  channelModel->InitChannelMatrix (channelNodes);

  // ---------------------------------------------------------------- devices
  NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (ues);
  std::set<double> rris;
  for (const FlowSpec &f : flows)
    {
      rris.insert (f.rriMs);
    }
  for (uint32_t i = 0; i < ues.GetN (); ++i)
    {
      Ptr<NrV2XUeMac> mac = ues.Get (i)->GetDevice (0)->GetObject<NistLteUeNetDevice> ()->GetMac ();
      for (double rri : rris)
        {
          mac->PushNewRRIValue (rri);
        }
    }

  InternetStackHelper internet;
  internet.Install (ues);
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.0.0", "255.255.0.0");
  ipv4.Assign (ueDevs);
  Ipv4StaticRoutingHelper routing;
  for (uint32_t u = 0; u < ues.GetN (); ++u)
    {
      routing.GetStaticRouting (ues.Get (u)->GetObject<Ipv4> ())->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }
  lteHelper->Attach (ueDevs);
  BuildingsHelper::Install (ues);
  if (jammerNode)
    {
      BuildingsHelper::Install (jammerNode); // MakeMobilityModelConsistent visits every node
    }
  BuildingsHelper::MakeMobilityModelConsistent ();

  // ---------------------------------------------------------------- flows
  std::ofstream flowMap ((outputPath + "FlowMap.txt").c_str ());
  for (uint32_t k = 0; k < flows.size (); ++k)
    {
      std::ostringstream address;
      address << "225.0.0." << (k + 1);
      flows[k].group = Ipv4Address (address.str ().c_str ());
      flows[k].port = 8000 + k;
      flows[k].l2 = k + 1;
      flowMap << k << "," << flows[k].name << "," << flows[k].pqi << "," << (int) flows[k].priority << "," << flows[k].pdb
              << "," << flows[k].per << "," << flows[k].gbr << "," << (flows[k].sps ? "sps" : "dynamic") << ","
              << flows[k].rriMs << "," << flows[k].l2 << "," << flows[k].retx << "\n";
      Ptr<NistSlTft> tft = Create<NistSlTft> (NistSlTft::BIDIRECTIONAL, flows[k].group, flows[k].l2);
      proseHelper->ActivateSidelinkBearer (Seconds (0.001), ueDevs, tft);
      PacketSinkHelper sink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), flows[k].port));
      ApplicationContainer sinks = sink.Install (ues);
      sinks.Start (Seconds (0.001));
      sinks.Stop (Seconds (simTime + 0.9));
    }
  flowMap.close ();

  Ptr<UniformRandomVariable> pick = CreateObject<UniformRandomVariable> ();
  std::ofstream nodeMap ((outputPath + "NodeMap.txt").c_str ());
  for (uint32_t i = 0; i < ueCount; ++i)
    {
      nodeMap << ues.Get (i)->GetId () << "," << roles[i].role << "," << roles[i].group << "," << roles[i].position << ",";
      for (uint32_t k = 0; k < flows.size (); ++k)
        {
          if (flows[k].roles.count (roles[i].role) == 0 || pick->GetValue (0, 1) >= flows[k].share)
            {
              continue;
            }
          Ptr<QosFlowApp> app = CreateObject<QosFlowApp> ();
          app->Setup (&flows[k], k, i);
          ues.Get (i)->AddApplication (app);
          app->SetStartTime (Seconds (0.01));
          app->SetStopTime (Seconds (simTime));
          nodeMap << k << " ";
        }
      nodeMap << "\n";
    }
  nodeMap.close ();

  // ---------------------------------------------------------------- impairments
  std::ofstream impairments ((outputPath + "Impairments.txt").c_str ());
  if (jammerNode)
    {
      Ptr<NrV2XUePhy> phy = ues.Get (0)->GetDevice (0)->GetObject<NistLteUeNetDevice> ()->GetPhy ();
      std::vector<int> allRbs;
      for (int rb = 0; rb < channelRbs; ++rb)
        {
          allRbs.push_back (rb);
        }
      Ptr<SpectrumValue> psd = NrV2XSpectrumValueHelper::CreateUlTxPowerSpectralDensity (54900, channelRbs, jam[2], allRbs, slot,
                                                                                          15 * scsFactor, mcs, false);
      Ptr<WaveformGenerator> generator = CreateObject<WaveformGenerator> ();
      generator->SetChannel (phy->GetSlSpectrumPhy ()->GetChannel ());
      generator->SetMobility (jammerNode->GetObject<MobilityModel> ());
      generator->SetTxPowerSpectralDensity (psd);
      generator->SetPeriod (MilliSeconds (jam[4]));
      generator->SetDutyCycle (jam[3]);
      Simulator::Schedule (Seconds (jam[5]), &WaveformGenerator::Start, generator);
      Simulator::Schedule (Seconds (jam[6]), &WaveformGenerator::Stop, generator);
      impairments << "jammer," << jammer << "\n";
    }
  if (!burstLoss.empty ())
    {
      std::vector<std::string> parts = Split (burstLoss, ';');
      NS_ABORT_MSG_IF (parts.size () != 3, "--BurstLoss=roles|...;meanGoodS;meanBadS");
      std::set<std::string> targets;
      for (const std::string &role : Split (parts[0], '|'))
        {
          targets.insert (role);
        }
      for (uint32_t i = 0; i < ueCount; ++i)
        {
          if (targets.count (roles[i].role))
            {
              Ptr<NrV2XSpectrumPhy> rx = ues.Get (i)->GetDevice (0)->GetObject<NistLteUeNetDevice> ()->GetPhy ()->GetSlSpectrumPhy ();
              rx->SetAttribute ("BurstLossMeanGood", DoubleValue (std::stod (parts[1])));
              rx->SetAttribute ("BurstLossMeanBad", DoubleValue (std::stod (parts[2])));
            }
        }
      impairments << "burst_loss," << burstLoss << "\n";
    }
  for (const FlowSpec &f : flows)
    {
      if (f.stackDelayMs > 0 || f.stackJitterMs > 0)
        {
          impairments << "stack_latency," << f.name << "," << f.stackDelayMs << "," << f.stackJitterMs << "\n";
        }
    }
  impairments.close ();

  // ---------------------------------------------------------------- sidelink configuration
  Ptr<LteUeRrcSl> slConfiguration = CreateObject<LteUeRrcSl> ();
  slConfiguration->SetSlEnabled (true);
  NistLteRrcSap::SlPreconfiguration preconfiguration;
  preconfiguration.preconfigGeneral.carrierFreq = 54900;
  preconfiguration.preconfigGeneral.slBandwidth = channelRbs;
  preconfiguration.preconfigComm.nbPools = 1;
  NistSlPreconfigPoolFactory poolFactory;
  uint64_t pscchBitmap = 0x0;
  for (uint32_t i = 0; i < 8; i++)
    {
      pscchBitmap = pscchBitmap >> 1 | 0x8000000000;
    }
  poolFactory.SetControlBitmap (pscchBitmap);
  poolFactory.SetControlPeriod ("sf40");
  poolFactory.SetDataOffset (8);
  poolFactory.SetHaveUeSelectedResourceConfig (false);
  preconfiguration.preconfigComm.pools[0] = poolFactory.CreatePool ();
  slConfiguration->SetSlPreconfiguration (preconfiguration);
  lteHelper->InstallSidelinkConfiguration (ueDevs, slConfiguration);

  std::ofstream readme ((outputPath + "simREADME.txt").c_str ());
  readme << "QOS scenario\ntrace=" << traceDir << "\nflows=" << flowsFile << "\nsimTime=" << simTime << "\nseed=" << seed
         << "\nrun=" << runNumber << "\nnodes=" << ueCount << "\nnumerology=" << numerology << "\nbandwidth=" << channelBW
         << " MHz (" << channelRbs << " RBs)\nsubchannel=" << subchannelSize << "\nmcs=" << mcs << "\ntxPower=" << txPower
         << "\nrsrp=" << rsrpThreshold << "\nreEvaluation=" << reEvaluation << "\npreemption=" << preemption
         << "\npreemptionPriority=" << preemptionPriority << "\npriorityAware=" << priorityAware << "\nurban=" << urban
         << "\nbuildings=" << buildings << "\njammer=" << jammer << "\nburstLoss=" << burstLoss << "\n";
  readme.close ();

  if (progress)
    {
      Simulator::Schedule (Seconds (1), &PrintProgress, 1.0);
    }
  Simulator::Stop (Seconds (simTime + 1));
  Simulator::Run ();
  WriteTxLog (flows);
  Simulator::Destroy ();
  std::cout << "packets " << g_packetId << ", nodes " << ueCount << ", peak active " << mobility.GetPeakActiveVehicles () << std::endl;
  return 0;
}
