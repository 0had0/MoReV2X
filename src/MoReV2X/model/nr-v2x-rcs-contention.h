/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Repeated Contention Scheduling (RCS) for NR-V2X Mode 2.
 *
 * A. Rolich, M. Tricco, S. Paroli, M. Yildiz, A. Baiocchi, "Repeated Contention
 * Scheduling: A Novel Resource Allocation Algorithm Toward 6G Vehicular Networks",
 * MAIN 2026 (arXiv:2607.06103).
 *
 * Every frame of Tf (10 ms) starts with a contention time (1 ms). The K sub-channels
 * (SCs) of the rest of the frame are split into J groups of w = K / J SCs, and the M
 * contention sub-carriers into J subsets of m = floor(M / J). A backlogged UE picks a
 * group and plays r rounds: in each round it sends a tone on one of the m sub-carriers
 * of its group and listens to the others. It leaves the contention when it hears tones
 * on w or more distinct lower sub-carriers. The UE makes both choices: it gives its
 * group when it registers, and the contention asks it for its tone in each round
 * (NrV2XUeMac draws them from its NrV2XRcsChoice).
 * After the last round the remaining UEs are the winners; a winner that heard k < w
 * lower sub-carriers in the last round takes the k-th SC of its group (with J = K, the
 * single SC of the group). Two winners of one SC collide on it.
 *
 * This object is the air of the contention: it plays every frame for all UEs of the
 * simulation (the tones are a channel shared by everybody), with the UEs' choices.
 * Who hears whose tone is set by ToneMode:
 *   All    every UE hears every tone (the paper's simulation model)
 *   Range  a UE hears the tones sent within ToneRange metres
 *   Power  a UE hears a tone whose received power, TonePower minus the path loss of
 *          the scenario's propagation model, reaches ToneThreshold
 */

#ifndef NR_V2X_RCS_CONTENTION_H
#define NR_V2X_RCS_CONTENTION_H

#include <ns3/object.h>
#include <ns3/ptr.h>
#include <ns3/mobility-model.h>
#include <ns3/propagation-loss-model.h>
#include <ns3/callback.h>
#include <fstream>
#include <map>
#include <vector>

namespace ns3 {

class NrV2XRcsContention : public Object
{
public:
  enum ToneMode
  {
    TONE_ALL = 0,
    TONE_RANGE = 1,
    TONE_POWER = 2
  };

  static TypeId GetTypeId (void);
  NrV2XRcsContention ();
  virtual ~NrV2XRcsContention ();

  /// The one contention channel of the simulation, created on first use
  static Ptr<NrV2XRcsContention> Get (void);
  /// Forget the channel (called when the simulator is destroyed, and by the tests)
  static void Reset (void);

  /// Path loss used for the tones in ToneMode Power (the scenario's model)
  void SetPropagationLossModel (Ptr<PropagationLossModel> model);

  /// The UE's tone in a round: (round 0 .. r-1, sub-carriers per group m) -> 0 .. m-1
  typedef Callback<uint32_t, uint32_t, uint32_t> ToneCallback;

  /**
   * A backlogged UE joins the contention of frame `frame` in the group it chose
   * (0 .. J-1). The contention calls `tone` once per round the UE is still in.
   */
  void Register (uint64_t frame, uint32_t nodeId, Ptr<MobilityModel> mobility,
                 uint32_t group, ToneCallback tone);

  /// Leave the contention of `frame` before it is played (a UE that contends for another packet)
  void Unregister (uint64_t frame, uint32_t nodeId);

  /// After the contention time of `frame`: the SC index won (0 .. K-1), or -1
  int32_t GetResult (uint64_t frame, uint32_t nodeId);

  uint32_t GetGroups (void) const;
  uint32_t GetRounds (void) const;
  uint32_t GetSubcarriers (void) const;
  uint32_t GetScPerFrame (void) const;
  /// Sub-carriers per group, m = floor(M / J)
  uint32_t GetSubcarriersPerGroup (void) const;
  /// SCs per group, w = K / J
  uint32_t GetScPerGroup (void) const;

  /// For the tests: does `listener` hear the tone of `talker`?
  bool Hears (Ptr<MobilityModel> listener, Ptr<MobilityModel> talker) const;

protected:
  virtual void DoDispose (void);

private:
  struct Contender
  {
    uint32_t nodeId;
    Ptr<MobilityModel> mobility;
    uint32_t group;
    ToneCallback tone;
  };

  void Resolve (uint64_t frame);

  uint32_t m_groups;           ///< J
  uint32_t m_rounds;           ///< r
  uint32_t m_subcarriers;      ///< M
  uint32_t m_scPerFrame;       ///< K
  ToneMode m_toneMode;
  double m_toneRange;          ///< m
  double m_toneThreshold;      ///< dBm
  double m_tonePower;          ///< dBm
  std::string m_outputPath;
  Ptr<PropagationLossModel> m_loss;
  std::map<uint64_t, std::vector<Contender> > m_contenders;
  std::map<uint64_t, std::map<uint32_t, int32_t> > m_results;
  std::ofstream m_log;

  static Ptr<NrV2XRcsContention> s_instance;
};

} // namespace ns3

#endif /* NR_V2X_RCS_CONTENTION_H */
