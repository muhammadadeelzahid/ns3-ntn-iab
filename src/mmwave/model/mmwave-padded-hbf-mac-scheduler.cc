/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
*   Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
*   Copyright (c) 2015, NYU WIRELESS, Tandon School of Engineering, New York University
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License version 2 as
*   published by the Free Software Foundation;
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software
*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*   Author: Marco Miozzo <marco.miozzo@cttc.es>
*           Nicola Baldo  <nbaldo@cttc.es>
*
*   Modified by: Marco Mezzavilla < mezzavilla@nyu.edu>
*                         Sourjya Dutta <sdutta@nyu.edu>
*                         Russell Ford <russell.ford@nyu.edu>
*                         Menglei Zhang <menglei@nyu.edu>
*/



#include <ns3/log.h>
#include <ns3/abort.h>
#include <ns3/simulator.h>
#include "mmwave-padded-hbf-mac-scheduler.h"
#include <ns3/lte-common.h>
#include <ns3/boolean.h>
#include <stdlib.h>     /* abs */
#include "mmwave-mac-pdu-header.h"
#include "mmwave-mac-pdu-tag.h"
#include "mmwave-spectrum-value-helper.h"
#include <cmath>
#include <sstream>      // std::stringstream
#include <algorithm>
#include <fstream>      // std::ofstream

namespace ns3 {


NS_LOG_COMPONENT_DEFINE ("MmWavePaddedHbfMacScheduler");

NS_OBJECT_ENSURE_REGISTERED (MmWavePaddedHbfMacScheduler);

class MmWavePaddedHbfMacCschedSapProvider : public MmWaveMacCschedSapProvider
{
public:
  MmWavePaddedHbfMacCschedSapProvider (MmWavePaddedHbfMacScheduler* scheduler);

  // inherited from MmWaveMacCschedSapProvider
  virtual void CschedCellConfigReq (const struct MmWaveMacCschedSapProvider::CschedCellConfigReqParameters& params);
  virtual void CschedUeConfigReq (const struct MmWaveMacCschedSapProvider::CschedUeConfigReqParameters& params);
  virtual void CschedLcConfigReq (const struct MmWaveMacCschedSapProvider::CschedLcConfigReqParameters& params);
  virtual void CschedLcReleaseReq (const struct MmWaveMacCschedSapProvider::CschedLcReleaseReqParameters& params);
  virtual void CschedUeReleaseReq (const struct MmWaveMacCschedSapProvider::CschedUeReleaseReqParameters& params);

private:
  MmWavePaddedHbfMacCschedSapProvider ();
  MmWavePaddedHbfMacScheduler* m_scheduler;
};

MmWavePaddedHbfMacCschedSapProvider::MmWavePaddedHbfMacCschedSapProvider ()
{
}

MmWavePaddedHbfMacCschedSapProvider::MmWavePaddedHbfMacCschedSapProvider (MmWavePaddedHbfMacScheduler* scheduler)
  : m_scheduler (scheduler)
{
}

void
MmWavePaddedHbfMacCschedSapProvider::CschedCellConfigReq (const struct MmWaveMacCschedSapProvider::CschedCellConfigReqParameters& params)
{
  m_scheduler->DoCschedCellConfigReq (params);
}

void
MmWavePaddedHbfMacCschedSapProvider::CschedUeConfigReq (const struct MmWaveMacCschedSapProvider::CschedUeConfigReqParameters& params)
{
  m_scheduler->DoCschedUeConfigReq (params);
}


void
MmWavePaddedHbfMacCschedSapProvider::CschedLcConfigReq (const struct MmWaveMacCschedSapProvider::CschedLcConfigReqParameters& params)
{
  m_scheduler->DoCschedLcConfigReq (params);
}

void
MmWavePaddedHbfMacCschedSapProvider::CschedLcReleaseReq (const struct MmWaveMacCschedSapProvider::CschedLcReleaseReqParameters& params)
{
  m_scheduler->DoCschedLcReleaseReq (params);
}

void
MmWavePaddedHbfMacCschedSapProvider::CschedUeReleaseReq (const struct MmWaveMacCschedSapProvider::CschedUeReleaseReqParameters& params)
{
  m_scheduler->DoCschedUeReleaseReq (params);
}

class MmWavePaddedHbfMacSchedSapProvider : public MmWaveMacSchedSapProvider
{
public:
  MmWavePaddedHbfMacSchedSapProvider (MmWavePaddedHbfMacScheduler* sched);

  virtual void SchedDlRlcBufferReq (const struct MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters& params);
  virtual void SchedTriggerReq (const struct MmWaveMacSchedSapProvider::SchedTriggerReqParameters& params);
  virtual void SchedDlCqiInfoReq (const struct MmWaveMacSchedSapProvider::SchedDlCqiInfoReqParameters& params);
  virtual void SchedUlCqiInfoReq (const struct MmWaveMacSchedSapProvider::SchedUlCqiInfoReqParameters& params);
  virtual void SchedUlMacCtrlInfoReq (const struct MmWaveMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters& params);
  virtual void SchedSetMcs (int mcs);
private:
  MmWavePaddedHbfMacSchedSapProvider ();
  MmWavePaddedHbfMacScheduler* m_scheduler;
};

MmWavePaddedHbfMacSchedSapProvider::MmWavePaddedHbfMacSchedSapProvider ()
{
}

MmWavePaddedHbfMacSchedSapProvider::MmWavePaddedHbfMacSchedSapProvider (MmWavePaddedHbfMacScheduler* sched)
  : m_scheduler (sched)
{
}

void
MmWavePaddedHbfMacSchedSapProvider::SchedDlRlcBufferReq (const struct MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters& params)
{
  m_scheduler->DoSchedDlRlcBufferReq (params);
}

void
MmWavePaddedHbfMacSchedSapProvider::SchedTriggerReq (const struct MmWaveMacSchedSapProvider::SchedTriggerReqParameters& params)
{
  m_scheduler->DoSchedTriggerReq (params);
}

void
MmWavePaddedHbfMacSchedSapProvider::SchedDlCqiInfoReq (const struct MmWaveMacSchedSapProvider::SchedDlCqiInfoReqParameters& params)
{
  m_scheduler->DoSchedDlCqiInfoReq (params);
}

void
MmWavePaddedHbfMacSchedSapProvider::SchedUlCqiInfoReq (const struct MmWaveMacSchedSapProvider::SchedUlCqiInfoReqParameters& params)
{
  m_scheduler->DoSchedUlCqiInfoReq (params);
}

void
MmWavePaddedHbfMacSchedSapProvider::SchedUlMacCtrlInfoReq (const struct MmWaveMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters& params)
{
  m_scheduler->DoSchedUlMacCtrlInfoReq (params);
}

void
MmWavePaddedHbfMacSchedSapProvider::SchedSetMcs (int mcs)
{
  m_scheduler->DoSchedSetMcs (mcs);
}

const unsigned MmWavePaddedHbfMacScheduler::m_macHdrSize = 0;
const unsigned MmWavePaddedHbfMacScheduler::m_subHdrSize = 4;
const unsigned MmWavePaddedHbfMacScheduler::m_rlcHdrSize = 3;

const double MmWavePaddedHbfMacScheduler::m_berDl = 0.001;



MmWavePaddedHbfMacScheduler::MmWavePaddedHbfMacScheduler ()
  : m_nextRnti (0),
    m_subframeNo (0),
    m_tbUid (0),
    m_macSchedSapUser (0),
    m_macCschedSapUser (0),
    m_iabScheduler (false),
    m_split (false), // Enable split mode so busy mask is applied during scheduling
    m_maxSchedulingDelay (1)
{
  NS_LOG_FUNCTION (this);
  m_macSchedSapProvider = new MmWavePaddedHbfMacSchedSapProvider (this);
  m_macCschedSapProvider = new MmWavePaddedHbfMacCschedSapProvider (this);
  m_iabBackahulSapProvider = new MemberMmWaveUeMacCschedSapProvider<MmWavePaddedHbfMacScheduler> (this);

  m_iabBusySubframeAllocation.clear();
}

MmWavePaddedHbfMacScheduler::~MmWavePaddedHbfMacScheduler ()
{
  NS_LOG_FUNCTION (this);
}

void
MmWavePaddedHbfMacScheduler::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_wbCqiRxed.clear ();
  m_dlHarqProcessesDciInfoMap.clear ();
  m_dlHarqProcessesTimer.clear ();
  m_dlHarqProcessesRlcPduMap.clear ();
  m_dlHarqInfoList.clear ();
  m_ulHarqCurrentProcessId.clear ();
  m_ulHarqProcessesStatus.clear ();
  m_ulHarqProcessesTimer.clear ();
  m_ulHarqProcessesDciInfoMap.clear ();
  delete m_macCschedSapProvider;
  delete m_macSchedSapProvider;
}

TypeId
MmWavePaddedHbfMacScheduler::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MmWavePaddedHbfMacScheduler")
    .SetParent<MmWaveMacScheduler> ()
    .AddConstructor<MmWavePaddedHbfMacScheduler> ()
    .AddAttribute ("CqiTimerThreshold",
                   "The number of TTIs a CQI is valid (default 1000 - 1 sec.)",
                   UintegerValue (100),
                   MakeUintegerAccessor (&MmWavePaddedHbfMacScheduler::m_cqiTimersThreshold),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("HarqEnabled",
                   "Activate/Deactivate the HARQ [by default is active].",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MmWavePaddedHbfMacScheduler::m_harqOn),
                   MakeBooleanChecker ())
    .AddAttribute ("FixedMcsDl",
                   "Fix MCS to value set in McsDlDefault (for testing)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MmWavePaddedHbfMacScheduler::m_fixedMcsDl),
                   MakeBooleanChecker ())
    .AddAttribute ("McsDefaultDl",
                   "Fixed DL MCS (for testing)",
                   UintegerValue (1),
                   MakeUintegerAccessor (&MmWavePaddedHbfMacScheduler::m_mcsDefaultDl),
                   MakeUintegerChecker<uint8_t> ())
    .AddAttribute ("FixedMcsUl",
                   "Fix MCS to value set in McsUlDefault (for testing)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MmWavePaddedHbfMacScheduler::m_fixedMcsUl),
                   MakeBooleanChecker ())
    .AddAttribute ("McsDefaultUl",
                   "Fixed UL MCS (for testing)",
                   UintegerValue (1),
                   MakeUintegerAccessor (&MmWavePaddedHbfMacScheduler::m_mcsDefaultUl),
                   MakeUintegerChecker<uint8_t> ())
    .AddAttribute ("DlSchedOnly",
                   "Only schedule downlink traffic (for testing)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MmWavePaddedHbfMacScheduler::m_dlOnly),
                   MakeBooleanChecker ())
    .AddAttribute ("UlSchedOnly",
                   "Only schedule uplink traffic (for testing)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MmWavePaddedHbfMacScheduler::m_ulOnly),
                   MakeBooleanChecker ())
    .AddAttribute ("FixedTti",
                   "Fix slot size",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MmWavePaddedHbfMacScheduler::m_fixedTti),
                   MakeBooleanChecker ())
    .AddAttribute ("SymPerSlot",
                   "Number of symbols per slot in Fixed TTI mode",
                   UintegerValue (6),
                   MakeUintegerAccessor (&MmWavePaddedHbfMacScheduler::m_symPerSlot),
                   MakeUintegerChecker<uint8_t> ())
  ;

  return tid;
}

void
MmWavePaddedHbfMacScheduler::SetMacSchedSapUser (MmWaveMacSchedSapUser* sap)
{
  m_macSchedSapUser = sap;
}

void
MmWavePaddedHbfMacScheduler::SetMacCschedSapUser (MmWaveMacCschedSapUser* sap)
{
  m_macCschedSapUser = sap;
}


MmWaveMacSchedSapProvider*
MmWavePaddedHbfMacScheduler::GetMacSchedSapProvider ()
{
  return m_macSchedSapProvider;
}

MmWaveMacCschedSapProvider*
MmWavePaddedHbfMacScheduler::GetMacCschedSapProvider ()
{
  return m_macCschedSapProvider;
}

void
MmWavePaddedHbfMacScheduler::ConfigureCommonParameters (Ptr<MmWavePhyMacCommon> config)
{
  m_phyMacConfig = config;
  m_amc = CreateObject <MmWaveAmc> (m_phyMacConfig);
  m_numRbg = m_phyMacConfig->GetNumRb () / m_phyMacConfig->GetNumRbPerRbg ();
  m_numHarqProcess = m_phyMacConfig->GetNumHarqProcess ();
  m_harqTimeout = m_phyMacConfig->GetHarqTimeout ();
  m_numDataSymbols = m_phyMacConfig->GetSymbolsPerSubframe () -
    m_phyMacConfig->GetDlCtrlSymbols () - m_phyMacConfig->GetUlCtrlSymbols ();
  NS_ASSERT_MSG (m_phyMacConfig->GetNumRb () == 1, \
                 "System must be configured with numRb=1 for TDMA mode");

  for (unsigned i = 0; i < m_phyMacConfig->GetUlSchedDelay (); i++)
    {
      m_ulSfAllocInfo.push_back (SfAllocInfo (SfnSf (0, i, 0)));
    }

  for (unsigned i = 0; i < m_phyMacConfig->GetSubframesPerFrame(); i++)
  {
    m_iabBusySubframeAllocation.push_back(SfIabAllocInfo(SfnSf (0, i, 0), false, m_phyMacConfig->GetSymbolsPerSubframe ()));
    }
}

void
MmWavePaddedHbfMacScheduler::DoSchedDlRlcBufferReq (const struct MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters& params)
{
  NS_LOG_FUNCTION (this << params.m_rnti << (uint32_t) params.m_logicalChannelIdentity);
  // API generated by RLC for updating RLC parameters on a LC (tx and retx queues)
  std::list<MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it = m_rlcBufferReq.begin ();
  bool newLc = true;
  while (it != m_rlcBufferReq.end ())
    {
      // remove old entries of this UE-LC
      if (((*it).m_rnti == params.m_rnti)&&((*it).m_logicalChannelIdentity == params.m_logicalChannelIdentity))
        {
          it = m_rlcBufferReq.erase (it);
          newLc = false;
        }
      else
        {
          ++it;
        }
    }
  // add the new parameters
  m_rlcBufferReq.insert (it, params);
  NS_LOG_INFO ("BSR for RNTI " << params.m_rnti << " LC " << (uint16_t)params.m_logicalChannelIdentity << " RLC tx size " << params.m_rlcTransmissionQueueSize << " RLC retx size " << params.m_rlcRetransmissionQueueSize << " RLC stat size " <<  params.m_rlcStatusPduSize);
  // initialize statistics of the flow in case of new flows
  if (newLc == true)
    {
      m_wbCqiRxed.insert ( std::pair<uint16_t, uint8_t > (params.m_rnti, 1));   // only codeword 0 at this stage (SISO)
      // initialized to 1 (i.e., the lowest value for transmitting a signal)
      m_wbCqiTimers.insert ( std::pair<uint16_t, uint32_t > (params.m_rnti, m_cqiTimersThreshold));
    }
}

void
MmWavePaddedHbfMacScheduler::DoSchedDlCqiInfoReq (const struct MmWaveMacSchedSapProvider::SchedDlCqiInfoReqParameters& params)
{
  NS_LOG_FUNCTION (this);

  std::map <uint16_t,uint8_t>::iterator it;
  for (unsigned int i = 0; i < params.m_cqiList.size (); i++)
    {
      if ( params.m_cqiList.at (i).m_cqiType == DlCqiInfo::WB )
        {
          // wideband CQI reporting
          std::map <uint16_t,uint8_t>::iterator it;
          uint16_t rnti = params.m_cqiList.at (i).m_rnti;
          it = m_wbCqiRxed.find (rnti);
                      if (it == m_wbCqiRxed.end ())
              {
                // create the new entry
                m_wbCqiRxed.insert ( std::pair<uint16_t, uint8_t > (rnti, params.m_cqiList.at (i).m_wbCqi) ); // only codeword 0 at this stage (SISO)
                // generate correspondent timer
                m_wbCqiTimers.insert ( std::pair<uint16_t, uint32_t > (rnti, m_cqiTimersThreshold));
              }
          else
            {
              // update the CQI value
              (*it).second = params.m_cqiList.at (i).m_wbCqi;
              // update correspondent timer
              std::map <uint16_t,uint32_t>::iterator itTimers;
              itTimers = m_wbCqiTimers.find (rnti);
              (*itTimers).second = m_cqiTimersThreshold;
            }
        }
      else if ( params.m_cqiList.at (i).m_cqiType == DlCqiInfo::SB )
        {
          // subband CQI reporting high layer configured
          // Not used by RR Scheduler
        }
      else
        {
          NS_LOG_ERROR (this << " CQI type unknown");
        }
    }
}


void
MmWavePaddedHbfMacScheduler::DoSchedUlCqiInfoReq (const struct MmWaveMacSchedSapProvider::SchedUlCqiInfoReqParameters& params)
{
  NS_LOG_FUNCTION (this);

  unsigned frameNum = params.m_sfnSf.m_frameNum;
  unsigned subframeNum =  params.m_sfnSf.m_sfNum;
  unsigned startSymIdx =  params.m_sfnSf.m_slotNum;

  switch (params.m_ulCqi.m_type)
    {
    case UlCqiInfo::PUSCH:
      {
        std::map <uint32_t, struct AllocMapElem>::iterator itMap;
        std::map <uint16_t, struct UlCqiMapElem>::iterator itCqi;
        itMap = m_ulAllocationMap.find (params.m_sfnSf.Encode ());
        if (itMap == m_ulAllocationMap.end ())
          {
            NS_LOG_INFO (this << " Does not find info on allocation, size : " << m_ulAllocationMap.size ());
            return;
          }
        NS_ASSERT_MSG (itMap->second.m_rntiPerChunk.size () == m_phyMacConfig->GetTotalNumChunk (), "SINR chunk map must cover full BW in TDMA mode");
        for (unsigned i = 0; i < itMap->second.m_rntiPerChunk.size (); i++)
          {
            // convert from fixed point notation Sxxxxxxxxxxx.xxx to double
            //double sinr = LteFfConverter::fpS11dot3toDouble (params.m_ulCqi.m_sinr.at (i));
            itCqi = m_ueUlCqi.find (itMap->second.m_rntiPerChunk.at (i));
            if (itCqi == m_ueUlCqi.end ())
              {
                // create a new entry
                std::vector <double> newCqi;
                for (unsigned j = 0; j < m_phyMacConfig->GetTotalNumChunk (); j++)
                  {
                    unsigned chunkInd = i;
                    if (chunkInd == j)
                      {
                        newCqi.push_back (params.m_ulCqi.m_sinr.at (i));
                        NS_LOG_LOGIC ("UL CQI report for RNTI " << itMap->second.m_rntiPerChunk.at (i) << " chunk " << i << " SINR " << params.m_ulCqi.m_sinr.at (i) << \
                                     " frame " << frameNum << " subframe " << subframeNum << " startSym " << startSymIdx);
                      }
                    else
                      {
                        // initialize with NO_SINR value.
                        newCqi.push_back (30.0);
                      }
                  }
                m_ueUlCqi.insert (std::pair <uint16_t, struct UlCqiMapElem> (itMap->second.m_rntiPerChunk.at (i),
                                                                             UlCqiMapElem (newCqi, itMap->second.m_numSym, itMap->second.m_tbSize)) );
                // generate correspondent timer
                m_ueCqiTimers.insert (std::pair <uint16_t, uint32_t > (itMap->second.m_rntiPerChunk.at (i), m_cqiTimersThreshold));
              }
            else
              {
                // update the value
                (*itCqi).second.m_ueUlCqi.at (i) = params.m_ulCqi.m_sinr.at (i);
                (*itCqi).second.m_numSym = itMap->second.m_numSym;
                (*itCqi).second.m_tbSize = itMap->second.m_tbSize;
                // update correspondent timer
                std::map <uint16_t, uint32_t>::iterator itTimers;
                itTimers = m_ueCqiTimers.find (itMap->second.m_rntiPerChunk.at (i));
                (*itTimers).second = m_cqiTimersThreshold;

                NS_LOG_LOGIC ("UL CQI report for RNTI " << itMap->second.m_rntiPerChunk.at (i) << " chunk " << i << " SINR " << params.m_ulCqi.m_sinr.at (i) << \
                             " frame " << frameNum << " subframe " << subframeNum << " startSym " << startSymIdx);

              }

          }
        // remove obsolete info on allocation
        m_ulAllocationMap.erase (itMap);
      }
      break;
    default:
      NS_FATAL_ERROR ("Unknown type of UL-CQI");
    }
  return;
}


void
MmWavePaddedHbfMacScheduler::RefreshHarqProcesses ()
{
  NS_LOG_FUNCTION (this);

  std::map <uint16_t, DlHarqProcessesTimer_t>::iterator itTimers;
  for (itTimers = m_dlHarqProcessesTimer.begin (); itTimers != m_dlHarqProcessesTimer.end (); itTimers++)
    {
      for (uint16_t i = 0; i < m_phyMacConfig->GetNumHarqProcess (); i++)
        {
          if ((*itTimers).second.at (i) == m_phyMacConfig->GetHarqTimeout ())
            {             // reset HARQ process
              NS_LOG_INFO (this << " Reset HARQ proc " << i << " for RNTI " << (*itTimers).first);
              std::map <uint16_t, DlHarqProcessesStatus_t>::iterator itStat = m_dlHarqProcessesStatus.find ((*itTimers).first);
              if (itStat == m_dlHarqProcessesStatus.end ())
                {
                  NS_FATAL_ERROR ("No Process Id Status found for this RNTI " << (*itTimers).first);
                }
              (*itStat).second.at (i) = 0;
              (*itTimers).second.at (i) = 0;
            }
          else
            {
              (*itTimers).second.at (i)++;
            }
        }
    }

  std::map <uint16_t, UlHarqProcessesTimer_t>::iterator itTimers2;
  for (itTimers2 = m_ulHarqProcessesTimer.begin (); itTimers2 != m_ulHarqProcessesTimer.end (); itTimers2++)
    {
      for (uint16_t i = 0; i < m_phyMacConfig->GetNumHarqProcess (); i++)
        {
          if ((*itTimers2).second.at (i) == m_phyMacConfig->GetHarqTimeout ())
            {             // reset HARQ process
              NS_LOG_INFO (this << " Reset HARQ proc " << i << " for RNTI " << (*itTimers2).first);
              std::map <uint16_t, UlHarqProcessesStatus_t>::iterator itStat = m_ulHarqProcessesStatus.find ((*itTimers2).first);
              if (itStat == m_ulHarqProcessesStatus.end ())
                {
                  NS_FATAL_ERROR ("No Process Id Status found for this RNTI " << (*itTimers2).first);
                }
              (*itStat).second.at (i) = 0;
              (*itTimers2).second.at (i) = 0;
            }
          else
            {
              (*itTimers2).second.at (i)++;
            }
        }
    }

}

uint8_t
MmWavePaddedHbfMacScheduler::UpdateDlHarqProcessId (uint16_t rnti)
{
  NS_LOG_FUNCTION (this << rnti);


  if (m_harqOn == false)
    {
      uint8_t tbUid = m_tbUid;
      m_tbUid = (m_tbUid + 1) % m_phyMacConfig->GetNumHarqProcess ();
      return tbUid;
    }

//	std::map <uint16_t, uint8_t>::iterator it = m_dlHarqCurrentProcessId.find (rnti);
//	if (it == m_dlHarqCurrentProcessId.end ())
//	{
//		NS_FATAL_ERROR ("No Process Id found for this RNTI " << rnti);
//	}
  std::map <uint16_t, DlHarqProcessesStatus_t>::iterator itStat = m_dlHarqProcessesStatus.find (rnti);
  if (itStat == m_dlHarqProcessesStatus.end ())
    {
      NS_FATAL_ERROR ("No Process Id Statusfound for this RNTI " << rnti);
    }

  // search for available process ID, if none available return numHarqProcess
  uint8_t harqId = m_phyMacConfig->GetNumHarqProcess ();
  for (unsigned i = 0; i < m_phyMacConfig->GetNumHarqProcess (); i++)
    {
      if (itStat->second[i] == 0)
        {
          itStat->second[i] = 1;
          harqId = i;
          break;
        }
    }
  return harqId;

//	uint8_t i = (*it).second;
//	do
//	{
//		i = (i + 1) % m_phyMacConfig->GetNumHarqProcess ();
//	}
//	while ( ((*itStat).second.at (i) != 0)&&(i != (*it).second));
//	if ((*itStat).second.at (i) == 0)
//	{
//		(*it).second = i;
//		(*itStat).second.at (i) = 1;
//	}
//	else
//	{
//		return (m_phyMacConfig->GetNumHarqProcess () + 1); // return a not valid harq proc id
//	}
//
//	return ((*it).second);
}

uint8_t
MmWavePaddedHbfMacScheduler::UpdateUlHarqProcessId (uint16_t rnti)
{
  NS_LOG_FUNCTION (this << rnti);

  if (m_harqOn == false)
    {
      uint8_t tbUid = m_tbUid;
      m_tbUid = (m_tbUid + 1) % m_phyMacConfig->GetNumHarqProcess ();
      return tbUid;
    }

//	std::map <uint16_t, uint8_t>::iterator it = m_ulHarqCurrentProcessId.find (rnti);
//	if (it == m_ulHarqCurrentProcessId.end ())
//	{
//		NS_FATAL_ERROR ("No Process Id found for this RNTI " << rnti);
//	}
  std::map <uint16_t, UlHarqProcessesStatus_t>::iterator itStat = m_ulHarqProcessesStatus.find (rnti);
  if (itStat == m_ulHarqProcessesStatus.end ())
    {
//      NS_LOG_ERROR("No Process Id Statusfound for this RNTI " << rnti<<" implementing temporary fix");
//
//      //this error occurs when the UE drops connection and the mac layer keeps receiving its BSRs and passing them to the scheduler
//      MmWaveMacCschedSapProvider::CschedUeConfigReqParameters params;
//      params.m_rnti = rnti;
//      params.m_transmissionMode = 0;       // set to default value (SISO) for avoiding random initialization (valgrind error)
//      m_macCschedSapProvider->CschedUeConfigReq (params);
//      itStat = m_ulHarqProcessesStatus.find (rnti);
      NS_FATAL_ERROR ("No Process Id Statusfound for this RNTI " << rnti);
    }

  // search for available process ID, if none available return numHarqProcess+1
  uint8_t harqId = m_phyMacConfig->GetNumHarqProcess ();
  for (unsigned i = 0; i < m_phyMacConfig->GetNumHarqProcess (); i++)
    {
      if (itStat->second[i] == 0)
        {
          itStat->second[i] = 1;
          harqId = i;
          break;
        }
    }
  return harqId;
}

unsigned MmWavePaddedHbfMacScheduler::CalcMinTbSizeNumSym (unsigned mcs, unsigned bufSize, unsigned &tbSize)
{
  // bisection line search to find minimum number of slots needed to encode entire buffer
  MmWaveMacPduHeader dummyMacHeader;
  //unsigned macHdrSize = 10; //dummyMacHeader.GetSerializedSize ();
  int numSymLow = 0;
  int numSymHigh = m_phyMacConfig->GetSymbolsPerSubframe ();

  int diff = 0;
  tbSize = (m_amc->GetTbSizeFromMcsSymbols (mcs, numSymHigh) / 8);       // start with max value
  while ((unsigned)tbSize > bufSize)
    {
      diff = abs (numSymHigh - numSymLow) / 2;
      if (diff == 0)
        {
          tbSize = (m_amc->GetTbSizeFromMcsSymbols (mcs, numSymHigh) / 8);
          return numSymHigh;
        }
      tbSize = (m_amc->GetTbSizeFromMcsSymbols (mcs, numSymHigh - diff) / 8);
      if ((unsigned)tbSize > bufSize)
        {
          numSymHigh -= diff;
        }
      while ((unsigned)tbSize < bufSize)
        {
          diff = abs (numSymHigh - numSymLow) / 2;
          if (diff == 0)
            {
              tbSize = (m_amc->GetTbSizeFromMcsSymbols (mcs, numSymHigh) / 8);
              return numSymHigh;
            }
          //tmp2 = numSym;
          tbSize = (m_amc->GetTbSizeFromMcsSymbols (mcs, numSymLow + diff) / 8);
          if ((unsigned)tbSize < bufSize)
            {
              numSymLow += diff;
            }
        }
    }

  tbSize = (m_amc->GetTbSizeFromMcsSymbols (mcs, numSymHigh) / 8);
  return (unsigned)numSymHigh;
}

void
MmWavePaddedHbfMacScheduler::DoSchedTriggerReq (const struct MmWaveMacSchedSapProvider::SchedTriggerReqParameters& params)
{
  // Open log file for writing
  std::ofstream logFile;
  logFile.open("scheduler_logs.txt", std::ios::app);
  if (logFile.is_open()) {
    logFile << "Time: " << Simulator::Now().GetSeconds() << "s - this: " << this << " - ";
    logFile << "DoSchedTriggerReq started" << std::endl;
    logFile.close();
  }
  
  NS_LOG_DEBUG("m_rntiIabInfoMap size " << m_rntiIabInfoMap.size());
  WriteLogToFile("m_rntiIabInfoMap size " + std::to_string(m_rntiIabInfoMap.size()));
  uint16_t frameNum = params.m_sfnSf.m_frameNum;
  uint8_t sfNum = params.m_sfnSf.m_sfNum;
  //uint8_t slotNum = params.m_sfnSf.m_slotNum;

  MmWaveMacSchedSapUser::SchedConfigIndParameters ret;
  ret.m_sfnSf = params.m_sfnSf;
  ret.m_sfAllocInfo.m_sfnSf = ret.m_sfnSf;
  SfnSf ulSfn = ret.m_sfnSf;
  if (ret.m_sfnSf.m_sfNum + m_phyMacConfig->GetUlSchedDelay () >=  m_phyMacConfig->GetSubframesPerFrame ())
    {
      ulSfn.m_frameNum++;
    }
  ulSfn.m_sfNum = (ret.m_sfnSf.m_sfNum + m_phyMacConfig->GetUlSchedDelay ()) % m_phyMacConfig->GetSubframesPerFrame ();
  
  NS_LOG_DEBUG("UL scheduling delay calculation: current subframe " << (unsigned)ret.m_sfnSf.m_sfNum 
               << " + delay " << m_phyMacConfig->GetUlSchedDelay() 
               << " = UL subframe " << (unsigned)ulSfn.m_sfNum 
               << " in frame " << (unsigned)ulSfn.m_frameNum);
  WriteLogToFile("UL scheduling delay calculation: current subframe " + std::to_string((unsigned)ret.m_sfnSf.m_sfNum) 
               + " + delay " + std::to_string(m_phyMacConfig->GetUlSchedDelay()) 
               + " = UL subframe " + std::to_string((unsigned)ulSfn.m_sfNum) 
               + " in frame " + std::to_string((unsigned)ulSfn.m_frameNum));
  
  // For UL allocations, we need to use the UL subframe timing
  SfnSf ulSchedSfn = ulSfn;
  
  NS_LOG_DEBUG ("Scheduling DL frame " << (unsigned)frameNum << " subframe " << (unsigned)sfNum
                                       << " UL frame " << (unsigned)ulSfn.m_frameNum << " subframe " << (unsigned)ulSfn.m_sfNum);
  WriteLogToFile("Scheduling DL frame " + std::to_string((unsigned)frameNum) + " subframe " + std::to_string((unsigned)sfNum)
                                       + " UL frame " + std::to_string((unsigned)ulSfn.m_frameNum) + " subframe " + std::to_string((unsigned)ulSfn.m_sfNum));

  // add slot for DL control
  SlotAllocInfo dlCtrlSlot (0, SlotAllocInfo::DL_slotAllocInfo, SlotAllocInfo::CTRL, SlotAllocInfo::DIGITAL, 0, 0);
  dlCtrlSlot.m_dci.m_numSym = 1;
  dlCtrlSlot.m_dci.m_symStart = 0;
  dlCtrlSlot.m_dci.m_layerInd = 0;
  ret.m_sfAllocInfo.m_slotAllocInfo.push_back (dlCtrlSlot);
  int resvCtrl = m_phyMacConfig->GetDlCtrlSymbols () + m_phyMacConfig->GetUlCtrlSymbols ();
  int symAvail = m_phyMacConfig->GetNumEnbLayers () * ( m_phyMacConfig->GetSymbolsPerSubframe () - resvCtrl );

  //these cursors track the "free region" on each HBF layer
//  std::vector<uint32_t> nextSymAvailLayer, lastSymAvailLayer, numDlUeLayer, numUlUeLayer;
//  std::map<uint16_t,uint8_t> ueToLayerMapDl, ueToLayerMapUl;
//  std::map<uint16_t,uint8_t>::iterator itUeToLayerMap;
//  for (uint8_t i=0; i<m_phyMacConfig->GetNumEnbLayers (); i++)
//    {
//      nextSymAvailLayer.push_back (m_phyMacConfig->GetDlCtrlSymbols ());
//      lastSymAvailLayer.push_back (m_phyMacConfig->GetSymbolsPerSubframe ()-m_phyMacConfig->GetUlCtrlSymbols ());
//      numDlUeLayer.push_back (0); //does not count Rtx
//      numUlUeLayer.push_back (0); //does not count Rtx
//    }

  //these cursors track the allocation radio resources accross the symbols x layers 2D rectangle.
  std::vector<uint32_t> symAvailLayer;
  for (uint8_t i=0; i<m_phyMacConfig->GetNumEnbLayers (); i++)
    {
      symAvailLayer.push_back (m_phyMacConfig->GetSymbolsPerSubframe () - resvCtrl);
    }
  uint32_t nextSymAvail=m_phyMacConfig->GetDlCtrlSymbols ();
  uint32_t lastSymAvail=m_phyMacConfig->GetSymbolsPerSubframe ()-m_phyMacConfig->GetDlCtrlSymbols ()-m_phyMacConfig->GetUlCtrlSymbols ();//last symbol is reserved for UL CTRL
  uint32_t futureNextSymAvail=m_phyMacConfig->GetDlCtrlSymbols ();
  uint32_t futureLastSymAvail=m_phyMacConfig->GetSymbolsPerSubframe ()-m_phyMacConfig->GetDlCtrlSymbols ()-m_phyMacConfig->GetUlCtrlSymbols ();
  std::set<uint16_t> setUeInCurrentSymbolBlock;
  std::set<uint16_t>::iterator itSetUeQuery;

  uint8_t layerIdx = 0;

  //these variables store the slot allocation info we construct. At the end we will insert them in the final vector in a specific order.
  uint8_t tempDlslotIdx = 1;
  uint8_t tempUlSlotIdx = 0; //we count the UL slots with this temporary variable
  std::vector<std::deque <SlotAllocInfo> > tempDlslotAllocInfo, tempUlslotAllocInfo; //we insert all UL slots after all DL slots

  for (uint8_t i=0; i<m_phyMacConfig->GetNumEnbLayers (); i++)
    {
      tempDlslotAllocInfo.push_back (std::deque<SlotAllocInfo>());
      tempUlslotAllocInfo.push_back (std::deque<SlotAllocInfo>());
    }
  //uint8_t symIdx = m_phyMacConfig->GetDlCtrlSymbols ();      // symbols reserved for control at beginning of subframe

  // process received CQIs
  RefreshDlCqiMaps ();
  RefreshUlCqiMaps ();

  // Process DL HARQ feedback
  RefreshHarqProcesses ();

  // IAB: get the resources which are already set as busy
  symAvail = UpdateBusySymbolsForIab(sfNum, m_phyMacConfig->GetDlCtrlSymbols(), symAvail);
  WriteLogToFile("IAB: Updated busy symbols, symAvail = " + std::to_string(symAvail));

  //m_rlcBufferReq.sort (SortRlcBufferReq);     // sort list by RNTI
  // number of DL/UL flows for new transmissions (not HARQ RETX)
  int nFlowsDl = 0;
  int nFlowsUl = 0;
  int nFlowsAccessDl = 0;
  int nFlowsAccessUl = 0;
  int nFlowsBackhaulDl = 0;
  int nFlowsBackhaulUl = 0;
  std::map <uint16_t, struct UeSchedInfo> ueInfo;
  std::map <uint16_t, struct UeSchedInfo>::iterator itUeInfo;
  std::list<MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator itRlcBuf;

  // retrieve past HARQ retx buffered
  if (m_dlHarqInfoList.size () > 0 && params.m_dlHarqInfoList.size () > 0)
    {
      m_dlHarqInfoList.insert (m_dlHarqInfoList.end (), params.m_dlHarqInfoList.begin (), params.m_dlHarqInfoList.end ());
    }
  else if (params.m_dlHarqInfoList.size () > 0)
    {
      m_dlHarqInfoList = params.m_dlHarqInfoList;
    }
  if (m_ulHarqInfoList.size () > 0 && params.m_ulHarqInfoList.size () > 0)
    {
      m_ulHarqInfoList.insert (m_ulHarqInfoList.end (), params.m_ulHarqInfoList.begin (), params.m_ulHarqInfoList.end ());
    }
  else if (params.m_ulHarqInfoList.size () > 0)
    {
      m_ulHarqInfoList = params.m_ulHarqInfoList;
    }

  if (m_harqOn == false)                // Ignore HARQ feedback
    {
      m_dlHarqInfoList.clear ();
    }
  else
    {
      // Process DL HARQ feedback and assign slots for RETX if resources available
      std::vector< std::pair <uint8_t,uint32_t> > sortedDlHarqRetx; // the pair is ( TBsize , harqProcessIndexOfVector-m_dlHarqInfoList ), so that the harq processes are ordered by decreasing TBsize and easy recovery of harq process struct is possible
      std::vector< std::pair <uint8_t,uint32_t> > sortedUlHarqRetx; //

      std::vector <struct DlHarqInfo> dlInfoListUntxed;            // TBs not able to be retransmitted in this sf
      std::vector <struct UlHarqInfo> ulInfoListUntxed;

      //first we loop the pending HARQ info list
      for (unsigned i = 0; i < m_dlHarqInfoList.size (); i++)
        {
          uint8_t harqId = m_dlHarqInfoList.at (i).m_harqProcessId;
          uint16_t rnti = m_dlHarqInfoList.at (i).m_rnti;
          itUeInfo = ueInfo.find (rnti);
          std::map <uint16_t, UlHarqProcessesStatus_t>::iterator itStat = m_dlHarqProcessesStatus.find (rnti);
          if (itStat == m_dlHarqProcessesStatus.end ())
            {
              NS_FATAL_ERROR ("No HARQ status info found for UE " << rnti);
            }
          std::map <uint16_t, DlHarqRlcPduList_t>::iterator itRlcPdu =  m_dlHarqProcessesRlcPduMap.find (rnti);
          if (itRlcPdu == m_dlHarqProcessesRlcPduMap.end ())
            {
              NS_FATAL_ERROR ("Unable to find RlcPdcList in HARQ buffer for RNTI " << m_dlHarqInfoList.at (i).m_rnti);
            }
          if (m_dlHarqInfoList.at (i).m_harqStatus == DlHarqInfo::ACK || itStat->second.at (harqId) == 0)
            {             // acknowledgment or process timeout, reset process
              NS_LOG_DEBUG ("UE" << rnti << " DL harqId " << (unsigned)harqId << " HARQ-ACK received");
              itStat->second.at (harqId) = 0;                      // release process ID
              for (uint16_t k = 0; k < itRlcPdu->second.size (); k++)                           // clear RLC buffers
                {
                  itRlcPdu->second.at (harqId).clear ();
                }
              continue;
            }
          else if (m_dlHarqInfoList.at (i).m_harqStatus == DlHarqInfo::NACK)
            {
              std::map <uint16_t, DlHarqProcessesDciInfoList_t>::iterator itHarq = m_dlHarqProcessesDciInfoMap.find (rnti);
              if (itHarq == m_dlHarqProcessesDciInfoMap.end ())
                {
                  NS_FATAL_ERROR ("No DCI/HARQ buffer entry found for UE " << rnti);
                }
              DciInfoElementTdma dciInfoReTx = itHarq->second.at (harqId);
              NS_LOG_DEBUG ("UE" << rnti << " DL harqId " << (unsigned)harqId << " HARQ-NACK received, rv " << (unsigned)dciInfoReTx.m_rv);
              NS_ASSERT (harqId == dciInfoReTx.m_harqProcess);
              //NS_ASSERT(itStat->second.at (harqId) > 0);
              NS_ASSERT (itStat->second.at (harqId) - 1 == dciInfoReTx.m_rv);
              if (dciInfoReTx.m_rv == 3)                   // maximum number of retx reached -> drop process
                {
                  NS_LOG_INFO ("Max number of retransmissions reached -> drop process");
                  itStat->second.at (harqId) = 0;
                  for (uint16_t k = 0; k < (*itRlcPdu).second.size (); k++)
                    {
                      itRlcPdu->second.at (harqId).clear ();
                    }
                  continue;
                }
              else
                {
                  // (1) Check if the CQI has decreased. If no updated value available, use the same MCS minus 1.
                  //                        If CQI is below min threshold, drop process.
                  // (2) Calculate new number of symbols it will take to encode at lower MCS.
                  //			If this exceeds the total number of symbols, reTX with original parameters.
                  //                        If exceeds remaining symbols available in this subframe (but not total symbols in SF),
                  //			update DCI info and try scheduling in next SF.

                  /*std::map <uint16_t,uint8_t>::iterator itCqi = m_wbCqiRxed.find (itRlcBuf->m_rnti);
                  int cqi;
                  int mcsNew;
                  if (itCqi != m_wbCqiRxed.end ())
                  {
                          cqi = itCqi->second;
                          if (cqi == 0)
                          {
                                  NS_LOG_INFO ("CQI for reTX is below threshhold. Drop process");
                                  itStat->second.at (harqId) = 0;
                                  for (uint16_t k = 0; k < (*itRlcPdu).second.size (); k++)
                                  {
                                          itRlcPdu->second.at (harqId).clear ();
                                  }
                                  continue;
                          }
                          else
                          {
                                  mcsNew = m_amc->GetMcsFromCqi (cqi);  // get MCS
                          }
                  }
                  else
                  {
                          if(dciInfoReTx.m_mcs > 0)
                          {
                                  mcsNew = dciInfoReTx.m_mcs - 1;
                          }
                          else
                          {
                                  mcsNew = dciInfoReTx.m_mcs;
                          }
                  }
                  // compute number of symbols required
                  unsigned numSymReq;
                  if (mcsNew < dciInfoReTx.m_mcs)
                  {
                          numSymReq = m_amc->GetNumSymbolsFromTbsMcs (dciInfoReTx.m_tbSize, mcsNew);
                          while (numSymReq < symAvail && mcsNew < dciInfoReTx.m_mcs);
                          {
                                  mcsNew++;
                                  numSymReq = m_amc->GetNumSymbolsFromTbsMcs (dciInfoReTx.m_tbSize, mcsNew);
                          }
                          mcsNew--;
                          numSymReq = m_amc->GetNumSymbolsFromTbsMcs (dciInfoReTx.m_tbSize, mcsNew);
                  }
                  if (numSymReq <= (m_phyMacConfig->GetSymbolsPerSubframe () - resvCtrl))
                  {	// not enough symbols to encode TB at required MCS, attempt in later SF
                          dlInfoListUntxed.push_back (m_dlHarqInfoList.at (i));
                          continue;
                  }*/

                  // add a reference to this harq process to the list in increasing order of TB size in symbols
                  std::pair <uint8_t,uint32_t> cqiInfoToSort (dciInfoReTx.m_numSym , i); // i is used to access m_dlHarqInfoList.at(i)
                  sortedDlHarqRetx.insert(
                      std::lower_bound( sortedDlHarqRetx.begin(), sortedDlHarqRetx.end(), cqiInfoToSort, std::greater< std::pair <uint8_t,uint32_t> >() ), //log(n) search
                      cqiInfoToSort //vector insert
                  );
                }
            }
        }
      NS_LOG_LOGIC("Processed " <<  m_dlHarqInfoList.size () <<" DL harq processes, sorted "<< sortedDlHarqRetx.size() << " NACKs in descending size order ");
  WriteLogToFile("Processed " + std::to_string(m_dlHarqInfoList.size()) + " DL harq processes, sorted " + std::to_string(sortedDlHarqRetx.size()) + " NACKs in descending size order");
      // After we have sorted all DL-HARQ by TBsize, we allocate them in increasing sequential layer-time blocks (increasing layer first)
      layerIdx = 0;
      std::vector< std::pair <uint8_t,uint32_t> >::iterator itSortedHarq = sortedDlHarqRetx.begin();
      bool done = ( itSortedHarq == sortedDlHarqRetx.end() );

      while (! done ){
          uint32_t idxSortedHarq = itSortedHarq->second;
          uint16_t rnti = m_dlHarqInfoList.at ( idxSortedHarq ).m_rnti;
          //we must skip the harq items of UEs that already have other transmissions during the same symbol-time interval
          itSetUeQuery = setUeInCurrentSymbolBlock.find ( rnti );
          while ( itSetUeQuery != setUeInCurrentSymbolBlock.end() )
            {
              itSortedHarq++;
              if ( itSortedHarq == sortedDlHarqRetx.end() )
                {//if the skipping process reaches the end, there are no harq processes we can add in the sime symbol-time interval than the previous
                  //all symbols in the remaining layer-symbol 2D rectangle are "wasted" because we leave them empty
                  symAvail -= ( m_phyMacConfig->GetNumEnbLayers () - layerIdx ) * ( futureNextSymAvail - nextSymAvail);
                  for (uint8_t i=layerIdx; i<m_phyMacConfig->GetNumEnbLayers (); i++)
                    {
                      symAvailLayer[i] -= ( futureNextSymAvail - nextSymAvail);
                    }
                  layerIdx = 0;
                  setUeInCurrentSymbolBlock.clear ();
                  nextSymAvail = futureNextSymAvail; //compute the highest next sym available accross all layers
                  itSortedHarq = sortedDlHarqRetx.begin(); //since we go back to layer 0, we point to the first harq in the pending ordered list
                }
              idxSortedHarq = itSortedHarq->second;
              rnti = m_dlHarqInfoList.at ( idxSortedHarq ).m_rnti;
              itSetUeQuery = setUeInCurrentSymbolBlock.find ( rnti );
            }
          uint8_t harqId = m_dlHarqInfoList.at ( idxSortedHarq ).m_harqProcessId;
          std::map <uint16_t, DlHarqProcessesStatus_t>::iterator itStat = m_dlHarqProcessesStatus.find (rnti); //this is the second time this is searched, error check was done first time so here we know we will always succeed
          std::map <uint16_t, DlHarqProcessesDciInfoList_t>::iterator itHarq = m_dlHarqProcessesDciInfoMap.find (rnti); //this is the second time this is searched, error check was done first time so here we know we will always succeed
          DciInfoElementTdma dciInfoReTx = itHarq->second.at (harqId);


          if (symAvail == 0)
            {
              done = true;
              break;                    // no symbols left to allocate
            }
          // allocate retx if enough symbols are available
          if ( symAvailLayer[layerIdx] >= dciInfoReTx.m_numSym && (nextSymAvail + dciInfoReTx.m_numSym -1) <= lastSymAvail )
            {
              // IAB: check if it overlaps with busy resources
              if(!CheckOverlapWithBusyResources(nextSymAvail, dciInfoReTx.m_numSym, layerIdx))
            {
              if ( layerIdx == 0 )
                {//since harq processes are sorted, this update can always be performed at layer 0
                  NS_ASSERT (futureNextSymAvail <= m_phyMacConfig->GetSymbolsPerSubframe () - m_phyMacConfig->GetUlCtrlSymbols ());
                  futureNextSymAvail = nextSymAvail + dciInfoReTx.m_numSym;
                }
              else
                {
                  NS_ASSERT ( dciInfoReTx.m_numSym <= ( futureNextSymAvail - nextSymAvail ) );
                }
              symAvail -= futureNextSymAvail - nextSymAvail; //we transmit dciInfoReTx.m_numSym, but the next "free" symbol is further away at the begining of the next symbol-time block
              symAvailLayer[layerIdx] -= futureNextSymAvail - nextSymAvail;
              dciInfoReTx.m_layerInd = layerIdx;
              dciInfoReTx.m_symStart = nextSymAvail;
              dciInfoReTx.m_rv++;
              dciInfoReTx.m_ndi = 0;
              itHarq->second.at (harqId) = dciInfoReTx;
              itStat->second.at (harqId) = itStat->second.at (harqId) + 1;
              //                  SlotAllocInfo slotInfo (slotIdx++, SlotAllocInfo::DL_slotAllocInfo, SlotAllocInfo::CTRL_DATA, SlotAllocInfo::DIGITAL, itUeInfo->first, layerIdx);
              SlotAllocInfo slotInfo (tempDlslotIdx++, SlotAllocInfo::DL_slotAllocInfo, SlotAllocInfo::CTRL_DATA, SlotAllocInfo::DIGITAL, itUeInfo->first, layerIdx);
              slotInfo.m_dci = dciInfoReTx;
              NS_LOG_LOGIC ("UE" << dciInfoReTx.m_rnti << " gets DL slots " << (unsigned)dciInfoReTx.m_symStart << "-" << (unsigned)(dciInfoReTx.m_symStart + dciInfoReTx.m_numSym - 1) <<
                            " tbs " << dciInfoReTx.m_tbSize << " harqId " << (unsigned)dciInfoReTx.m_harqProcess << " harqId " << (unsigned)dciInfoReTx.m_harqProcess <<
                            " rv " << (unsigned)dciInfoReTx.m_rv << " in frame " << ret.m_sfnSf.m_frameNum << " subframe " << (unsigned)ret.m_sfnSf.m_sfNum << " layer " << (unsigned) dciInfoReTx.m_layerInd << " RETX");
              WriteLogToFile("UE " + std::to_string(dciInfoReTx.m_rnti) + " gets DL slots " + std::to_string((unsigned)dciInfoReTx.m_symStart) + "-" + std::to_string((unsigned)(dciInfoReTx.m_symStart + dciInfoReTx.m_numSym - 1)) +
                            " tbs " + std::to_string(dciInfoReTx.m_tbSize) + " harqId " + std::to_string((unsigned)dciInfoReTx.m_harqProcess) + " rv " + std::to_string((unsigned)dciInfoReTx.m_rv) + " in frame " + std::to_string(ret.m_sfnSf.m_frameNum) + " subframe " + std::to_string((unsigned)ret.m_sfnSf.m_sfNum) + " layer " + std::to_string((unsigned)dciInfoReTx.m_layerInd) + " RETX");
              std::map <uint16_t, DlHarqRlcPduList_t>::iterator itRlcList =  m_dlHarqProcessesRlcPduMap.find (rnti);
              if ( itRlcList == m_dlHarqProcessesRlcPduMap.end () )
                {
                  NS_FATAL_ERROR ("Unable to find RlcPdcList in HARQ buffer for RNTI " << rnti);
                }
              for (uint16_t k = 0; k < (*itRlcList).second.at (dciInfoReTx.m_harqProcess).size (); k++)
                {
                  slotInfo.m_rlcPduInfo.push_back ((*itRlcList).second.at (dciInfoReTx.m_harqProcess).at (k));
                }

              //                  ret.m_sfAllocInfo.m_slotAllocInfo.push_back (slotInfo);
              tempDlslotAllocInfo[layerIdx].push_back (slotInfo); //
              ret.m_sfAllocInfo.m_numSymAlloc += dciInfoReTx.m_numSym;
              if (itUeInfo == ueInfo.end ())
                {
                  itUeInfo = ueInfo.insert (std::pair<uint16_t, struct UeSchedInfo> (rnti, UeSchedInfo () )).first;
                }
              itUeInfo->second.m_dlSymbolsRetx = dciInfoReTx.m_numSym;
              itUeInfo->second.m_dlHbfLayer=layerIdx;//TODO: verify if this parameter can go away in this scheduler, otherwise convert into a map with pairs (slot,layerIdx), since now layer index is not unique for all slots of the same UE

              //move the layer counter to the next layer
              layerIdx++;
              if ( layerIdx==m_phyMacConfig->GetNumEnbLayers () )
                {
                  layerIdx = 0;
                  nextSymAvail = futureNextSymAvail; //compute the highest next sym available accross all layers
                  setUeInCurrentSymbolBlock.clear ();
                }
              else
                {
                  setUeInCurrentSymbolBlock.insert ( rnti );
                }
              }
              else
              {
                // IAB: find if there are other resources later
                // we cannot split RETX!
                int numSymNeeded = dciInfoReTx.m_numSym;
                int numFreeSymbols = 0;
                uint8_t tmpSymIdx = nextSymAvail;

                while(numFreeSymbols == 0 && tmpSymIdx < m_phyMacConfig->GetSymbolsPerSubframe()-1) 
                {
                  numFreeSymbols = GetNumFreeSymbols(tmpSymIdx, numSymNeeded); // this is equal to numSymNeeded if there is no overlap, otherwise smaller
                  NS_LOG_LOGIC("RETX numSymNeeded " << numSymNeeded << " numFreeSymbols " 
                    << numFreeSymbols << " tmpSymIdx " << (uint16_t)tmpSymIdx);
                  if(numFreeSymbols < numSymNeeded)
                  {
                    numFreeSymbols = 0;
                    tmpSymIdx = GetFirstFreeSymbol(tmpSymIdx, numSymNeeded); // get the next symIdx
                    if((int)(tmpSymIdx + numSymNeeded) > (int)m_phyMacConfig->GetSymbolsPerSubframe()-1)
                    {
                      NS_LOG_DEBUG("No way to fit this retx in the available resources");
                      break;
                    }
                  }
                }

                NS_LOG_LOGIC("RETX numSymNeeded " << numSymNeeded << " numFreeSymbols " 
                  << numFreeSymbols << " tmpSymIdx " << (uint16_t)tmpSymIdx);
                if(numFreeSymbols >= numSymNeeded && (int)(tmpSymIdx + numSymNeeded) < (int)(m_phyMacConfig->GetSymbolsPerSubframe () - m_phyMacConfig->GetUlCtrlSymbols ()))
                {
                  NS_LOG_LOGIC("Found resources for DL HARQ RETX");
                  symAvail -= dciInfoReTx.m_numSym;
                  dciInfoReTx.m_symStart = tmpSymIdx;

                  NS_LOG_LOGIC("Before updating " << PrintSubframeAllocationMask(m_busyResourcesSchedSubframe.m_symAllocationMask));

                  // update resource mask
                  UpdateResourceMask(tmpSymIdx, dciInfoReTx.m_numSym);

                  NS_LOG_LOGIC("After updating " << PrintSubframeAllocationMask(m_busyResourcesSchedSubframe.m_symAllocationMask));

                  dciInfoReTx.m_layerInd = layerIdx;
                  dciInfoReTx.m_rv++;
                  dciInfoReTx.m_ndi = 0;
                  itHarq->second.at (harqId) = dciInfoReTx;
                  itStat->second.at (harqId) = itStat->second.at (harqId) + 1;
                  SlotAllocInfo slotInfo (tempDlslotIdx++, SlotAllocInfo::DL_slotAllocInfo, SlotAllocInfo::CTRL_DATA, SlotAllocInfo::DIGITAL, itUeInfo->first, layerIdx);
                  slotInfo.m_dci = dciInfoReTx;
                  NS_LOG_LOGIC ("UE" << dciInfoReTx.m_rnti << " gets DL slots " << (unsigned)dciInfoReTx.m_symStart << "-" << (unsigned)(dciInfoReTx.m_symStart + dciInfoReTx.m_numSym - 1) <<
                                " tbs " << dciInfoReTx.m_tbSize << " harqId " << (unsigned)dciInfoReTx.m_harqProcess << " harqId " << (unsigned)dciInfoReTx.m_harqProcess <<
                                " rv " << (unsigned)dciInfoReTx.m_rv << " in frame " << ret.m_sfnSf.m_frameNum << " subframe " << (unsigned)ret.m_sfnSf.m_sfNum << " layer " << (unsigned) dciInfoReTx.m_layerInd << " RETX");
                  std::map <uint16_t, DlHarqRlcPduList_t>::iterator itRlcList =  m_dlHarqProcessesRlcPduMap.find (rnti);
                  if ( itRlcList == m_dlHarqProcessesRlcPduMap.end () )
                    {
                      NS_FATAL_ERROR ("Unable to find RlcPdcList in HARQ buffer for RNTI " << rnti);
                    }
                  for (uint16_t k = 0; k < (*itRlcList).second.at (dciInfoReTx.m_harqProcess).size (); k++)
                    {
                      slotInfo.m_rlcPduInfo.push_back ((*itRlcList).second.at (dciInfoReTx.m_harqProcess).at (k));
                    }

                  tempDlslotAllocInfo[layerIdx].push_back (slotInfo);
                  ret.m_sfAllocInfo.m_numSymAlloc += dciInfoReTx.m_numSym;
                  if (itUeInfo == ueInfo.end ())
                    {
                      itUeInfo = ueInfo.insert (std::pair<uint16_t, struct UeSchedInfo> (rnti, UeSchedInfo () )).first;
                    }
                  itUeInfo->second.m_dlSymbolsRetx = dciInfoReTx.m_numSym;
                  itUeInfo->second.m_dlHbfLayer=layerIdx;

                  //move the layer counter to the next layer
                  layerIdx++;
                  if ( layerIdx==m_phyMacConfig->GetNumEnbLayers () )
                    {
                      layerIdx = 0;
                      nextSymAvail = futureNextSymAvail; //compute the highest next sym available accross all layers
                      setUeInCurrentSymbolBlock.clear ();
                    }
                  else
                    {
                      setUeInCurrentSymbolBlock.insert ( rnti );
                    }
                }
                else
                {
                  NS_LOG_DEBUG ("No resource for this retx (even later) -> buffer it");
                  dlInfoListUntxed.push_back (m_dlHarqInfoList.at ( idxSortedHarq ));
                }
                }
            }
          else
            {
              NS_LOG_INFO ("No resource for this retx -> buffer it");
              dlInfoListUntxed.push_back (m_dlHarqInfoList.at ( idxSortedHarq ));
            }

          sortedDlHarqRetx.erase(itSortedHarq);//remove the current item that has been either allocated or determined to not fit
          itSortedHarq=sortedDlHarqRetx.begin();
          if ( itSortedHarq==sortedDlHarqRetx.end() )
            {
              done = true;
              break;
            }
      }

      m_dlHarqInfoList.clear ();
      m_dlHarqInfoList = dlInfoListUntxed;

      //If we did not end pointing to a new layer-symbol slot, update the highest next sym available accross all layers
      //all symbols in the remaining layer-symbol 2D rectangle are "wasted" because we leave them empty
      if ( layerIdx > 0 )
        {
          symAvail -= ( m_phyMacConfig->GetNumEnbLayers () - layerIdx ) * ( futureNextSymAvail - nextSymAvail);
          for (uint8_t i=layerIdx; i<m_phyMacConfig->GetNumEnbLayers (); i++)
            {
              symAvailLayer[i] -= ( futureNextSymAvail - nextSymAvail );
            }
          layerIdx = 0;
          setUeInCurrentSymbolBlock.clear ();
          nextSymAvail = futureNextSymAvail;
        }

      // Process UL HARQ feedback
      for (uint16_t i = 0; i < m_ulHarqInfoList.size (); i++)
        {
          UlHarqInfo harqInfo = m_ulHarqInfoList.at (i);
          uint8_t harqId = harqInfo.m_harqProcessId;
          uint16_t rnti = harqInfo.m_rnti;
          itUeInfo = ueInfo.find (rnti);
          std::map <uint16_t, UlHarqProcessesStatus_t>::iterator itStat = m_ulHarqProcessesStatus.find (rnti);
          if (itStat == m_ulHarqProcessesStatus.end ())
            {
              NS_LOG_ERROR ("No info found in HARQ buffer for UE (might have changed eNB) " << rnti);
            }
          if (harqInfo.m_receptionStatus == UlHarqInfo::Ok || itStat->second.at (harqId) == 0)
            {
              NS_LOG_DEBUG ("UE" << rnti << " UL harqId " << (unsigned)harqInfo.m_harqProcessId << " HARQ-ACK received");
              if (itStat != m_ulHarqProcessesStatus.end ())
                {
                  itStat->second.at (harqId) = 0;                        // release process ID
                }
            }
          else if (harqInfo.m_receptionStatus == UlHarqInfo::NotOk)
            {
              std::map <uint16_t, UlHarqProcessesDciInfoList_t>::iterator itHarq = m_ulHarqProcessesDciInfoMap.find (rnti);
              if (itHarq == m_ulHarqProcessesDciInfoMap.end ())
                {
                  NS_LOG_ERROR ("No info found in UL-HARQ buffer for UE (might have changed eNB) " << rnti);
                }
              // retx correspondent block: retrieve the UL-DCI
              DciInfoElementTdma dciInfoReTx = itHarq->second.at (harqId);
              NS_LOG_DEBUG ("UE" << rnti << " UL harqId " << (unsigned)harqInfo.m_harqProcessId << " HARQ-NACK received, rv " << (unsigned)dciInfoReTx.m_rv);
              NS_ASSERT (harqId == dciInfoReTx.m_harqProcess);
              NS_ASSERT (itStat->second.at (harqId) > 0);
              NS_ASSERT (itStat->second.at (harqId) - 1 == dciInfoReTx.m_rv);
              if (dciInfoReTx.m_rv == 3)
                {
                  NS_LOG_INFO ("Max number of retransmissions reached (UL)-> drop process");
                  itStat->second.at (harqId) = 0;
                  continue;
                }
              else
                {
                  std::pair <uint8_t,uint32_t> cqiInfoToSort (dciInfoReTx.m_numSym , i); // i is used to access m_dlHarqInfoList.at(i)
                  sortedUlHarqRetx.insert(
                      std::lower_bound( sortedUlHarqRetx.begin(), sortedUlHarqRetx.end(), cqiInfoToSort , std::greater< std::pair <uint8_t,uint32_t> >() ), //log(n) search
                      cqiInfoToSort //vector insert
                  );
                }
            }
        }

      NS_LOG_LOGIC("Processed " <<  m_ulHarqInfoList.size () <<" UL harq processes, sorted "<< sortedUlHarqRetx.size() << " NACKs in descending size order ");
  WriteLogToFile("Processed " + std::to_string(m_ulHarqInfoList.size()) + " UL harq processes, sorted " + std::to_string(sortedUlHarqRetx.size()) + " NACKs in descending size order");
      layerIdx = 0;
      itSortedHarq = sortedUlHarqRetx.begin();
      done = ( itSortedHarq == sortedUlHarqRetx.end() );
      while (! done ){
          uint32_t idxSortedHarq = itSortedHarq->second;
          uint16_t rnti = m_ulHarqInfoList.at ( idxSortedHarq ).m_rnti;
          //we must skip the harq items of UEs that already have other transmissions during the same symbol-time interval
          itSetUeQuery = setUeInCurrentSymbolBlock.find ( rnti );
          while ( itSetUeQuery != setUeInCurrentSymbolBlock.end() )
            {
              itSortedHarq++;
              if ( itSortedHarq == sortedUlHarqRetx.end() )
                {//if the skipping process reaches the end, there are no harq processes we can add in the same symbol-time interval than the previous
                  //all symbols in the remaining layer-symbol 2D rectangle are "wasted" because we leave them empty
                  symAvail -= ( m_phyMacConfig->GetNumEnbLayers () - layerIdx ) * ( lastSymAvail - futureLastSymAvail );
                  for (uint8_t i=layerIdx; i < m_phyMacConfig->GetNumEnbLayers () ; i++)
                    {
                      symAvailLayer[i] -= ( lastSymAvail - futureLastSymAvail );
                    }
                  layerIdx = 0;
                  lastSymAvail = futureLastSymAvail;
                  setUeInCurrentSymbolBlock.clear ();
                  itSortedHarq = sortedUlHarqRetx.begin();
                }
              idxSortedHarq = itSortedHarq->second;
              rnti = m_ulHarqInfoList.at ( idxSortedHarq ).m_rnti;
              itSetUeQuery = setUeInCurrentSymbolBlock.find ( rnti );
            }
          uint8_t harqId = m_ulHarqInfoList.at ( idxSortedHarq ).m_harqProcessId;
          std::map <uint16_t, UlHarqProcessesStatus_t>::iterator itStat = m_ulHarqProcessesStatus.find (rnti); //this is the second time this is searched, error check was done first time so here we know we will always succeed
          std::map <uint16_t, DlHarqProcessesDciInfoList_t>::iterator itHarq = m_ulHarqProcessesDciInfoMap.find (rnti); //this is the second time this is searched, error check was done first time so here we know we will always succeed
          DciInfoElementTdma dciInfoReTx = itHarq->second.at (harqId);

          if (symAvail == 0)
            {
              done = true;
              break;                    // no symbols left to allocate
            }
          if  ( symAvailLayer[layerIdx] >= dciInfoReTx.m_numSym && ( lastSymAvail - dciInfoReTx.m_numSym +1) >= nextSymAvail)
            {
              // IAB: check if it overlaps with busy resources
              if(!CheckOverlapWithBusyResources(futureLastSymAvail + 1, dciInfoReTx.m_numSym, layerIdx))
            {
              if ( layerIdx == 0 )
                {//since harq processes are sorted, this update can always be performed at layer 0
                  NS_ASSERT ( lastSymAvail >= dciInfoReTx.m_numSym );
                  futureLastSymAvail = lastSymAvail - dciInfoReTx.m_numSym;
                }
              else
                {
                  NS_ASSERT ( nextSymAvail - dciInfoReTx.m_numSym >= futureNextSymAvail );
                }
              symAvail -= lastSymAvail - futureLastSymAvail ; //we transmit dciInfoReTx.m_numSym, but the next "free" symbol is further away at the beginning of the next symbol-time block
              symAvailLayer[layerIdx] -= lastSymAvail - futureLastSymAvail ;
              dciInfoReTx.m_layerInd = layerIdx;
              dciInfoReTx.m_symStart = futureLastSymAvail + 1; //time alignment across all layers at the block start
              dciInfoReTx.m_rv++;
              dciInfoReTx.m_ndi = 0;
              itStat->second.at (harqId) = itStat->second.at (harqId) + 1;
              itHarq->second.at (harqId) = dciInfoReTx;
              SlotAllocInfo slotInfo (tempUlSlotIdx++, SlotAllocInfo::UL_slotAllocInfo, SlotAllocInfo::CTRL_DATA, SlotAllocInfo::DIGITAL, itUeInfo->first, layerIdx);
              slotInfo.m_dci = dciInfoReTx;
              NS_LOG_LOGIC ("UE" << dciInfoReTx.m_rnti << " gets UL slots " << (unsigned)dciInfoReTx.m_symStart << "-" << (unsigned)(dciInfoReTx.m_symStart + dciInfoReTx.m_numSym - 1) <<
                            " tbs " << dciInfoReTx.m_tbSize << " harqId " << (unsigned)dciInfoReTx.m_harqProcess << " rv " << (unsigned)dciInfoReTx.m_rv << " in frame " << ulSfn.m_frameNum << " subframe " << (unsigned)ulSfn.m_sfNum << " layer " << (unsigned) dciInfoReTx.m_layerInd <<
                            " RETX");
              //                  ret.m_sfAllocInfo.m_slotAllocInfo.push_back (slotInfo);
              tempUlslotAllocInfo[layerIdx].push_front (slotInfo); //remember we fill from
              ret.m_sfAllocInfo.m_numSymAlloc += dciInfoReTx.m_numSym;
              if (itUeInfo == ueInfo.end ())
                {
                  itUeInfo = ueInfo.insert (std::pair<uint16_t, struct UeSchedInfo> (rnti, UeSchedInfo () )).first;
                }
              itUeInfo->second.m_ulSymbolsRetx = dciInfoReTx.m_numSym;
              itUeInfo->second.m_ulHbfLayer=layerIdx;//this parameter can go away in this scheduler

              //move the layer counter to the next layer
              layerIdx++;
              if ( layerIdx==m_phyMacConfig->GetNumEnbLayers () )
                {
                  layerIdx = 0;
                  lastSymAvail = futureLastSymAvail;
                  setUeInCurrentSymbolBlock.clear ();
                }
              else
                {
                  setUeInCurrentSymbolBlock.insert ( rnti );
                }
              }
              else
              {
                // IAB: find if there are other resources later
                // we cannot split RETX!
                int numSymNeeded = dciInfoReTx.m_numSym;
                int numFreeSymbols = 0;
                uint8_t tmpSymIdx = futureLastSymAvail + 1;

                while(numFreeSymbols == 0 && tmpSymIdx < m_phyMacConfig->GetSymbolsPerSubframe()-1) 
                {
                  numFreeSymbols = GetNumFreeSymbols(tmpSymIdx, numSymNeeded); // this is equal to numSymNeeded if there is no overlap, otherwise smaller
                  NS_LOG_LOGIC("UL RETX numSymNeeded " << numSymNeeded << " numFreeSymbols " 
                    << numFreeSymbols << " tmpSymIdx " << (uint16_t)tmpSymIdx);
                  if(numFreeSymbols < numSymNeeded)
                  {
                    numFreeSymbols = 0;
                    tmpSymIdx = GetFirstFreeSymbol(tmpSymIdx, numSymNeeded); // get the next symIdx
                    if((int)(tmpSymIdx + numSymNeeded) >  (int)m_phyMacConfig->GetSymbolsPerSubframe()-1)
                    {	
                      NS_LOG_DEBUG("No way to fit this retx in the available resources");
                      break;
                    }
                  }
                }

                NS_LOG_LOGIC("UL RETX numSymNeeded " << numSymNeeded << " numFreeSymbols " 
                  << numFreeSymbols << " tmpSymIdx " << (uint16_t)tmpSymIdx);
                if(numFreeSymbols >= numSymNeeded && (int)(tmpSymIdx + numSymNeeded) < (int)(m_phyMacConfig->GetSymbolsPerSubframe () - m_phyMacConfig->GetUlCtrlSymbols ()))
                {
                  symAvail -= dciInfoReTx.m_numSym;
                  dciInfoReTx.m_symStart = tmpSymIdx;

                  NS_LOG_LOGIC("Before updating " << PrintSubframeAllocationMask(m_busyResourcesSchedSubframe.m_symAllocationMask));

                  // update resource mask
                  UpdateResourceMask(tmpSymIdx, dciInfoReTx.m_numSym);

                  NS_LOG_LOGIC("After updating " << PrintSubframeAllocationMask(m_busyResourcesSchedSubframe.m_symAllocationMask));

                  dciInfoReTx.m_layerInd = layerIdx;
                  dciInfoReTx.m_rv++;
                  dciInfoReTx.m_ndi = 0;
                  itStat->second.at (harqId) = itStat->second.at (harqId) + 1;
                  itHarq->second.at (harqId) = dciInfoReTx;
                  SlotAllocInfo slotInfo (tempUlSlotIdx++, SlotAllocInfo::UL_slotAllocInfo, SlotAllocInfo::CTRL_DATA, SlotAllocInfo::DIGITAL, itUeInfo->first, layerIdx);
                  slotInfo.m_dci = dciInfoReTx;
                  NS_LOG_LOGIC ("UE" << dciInfoReTx.m_rnti << " gets UL slots " << (unsigned)dciInfoReTx.m_symStart << "-" << (unsigned)(dciInfoReTx.m_symStart + dciInfoReTx.m_numSym - 1) <<
                                " tbs " << dciInfoReTx.m_tbSize << " harqId " << (unsigned)dciInfoReTx.m_harqProcess << " rv " << (unsigned)dciInfoReTx.m_rv << " in frame " << ulSfn.m_frameNum << " subframe " << (unsigned)ulSfn.m_sfNum << " layer " << (unsigned) dciInfoReTx.m_layerInd <<
                                " RETX");
                  tempUlslotAllocInfo[layerIdx].push_front (slotInfo); //remember we fill from
                  ret.m_sfAllocInfo.m_numSymAlloc += dciInfoReTx.m_numSym;
                  if (itUeInfo == ueInfo.end ())
                    {
                      itUeInfo = ueInfo.insert (std::pair<uint16_t, struct UeSchedInfo> (rnti, UeSchedInfo () )).first;
                    }
                  itUeInfo->second.m_ulSymbolsRetx = dciInfoReTx.m_numSym;
                  itUeInfo->second.m_ulHbfLayer=layerIdx;

                  //move the layer counter to the next layer
                  layerIdx++;
                  if ( layerIdx==m_phyMacConfig->GetNumEnbLayers () )
                    {
                      layerIdx = 0;
                      lastSymAvail = futureLastSymAvail;
                      setUeInCurrentSymbolBlock.clear ();
                    }
                  else
                    {
                      setUeInCurrentSymbolBlock.insert ( rnti );
                    }
                }
                else
                {
                  NS_LOG_DEBUG ("No resource for this UL retx (even later) -> buffer it");
                  ulInfoListUntxed.push_back (m_ulHarqInfoList.at ( idxSortedHarq ));
                }
                }
            }
          else
            {
              ulInfoListUntxed.push_back (m_ulHarqInfoList.at ( idxSortedHarq ));
            }

          sortedUlHarqRetx.erase(itSortedHarq);//remove the current item that has been either allocated or determined to not fit
          itSortedHarq=sortedUlHarqRetx.begin();
          if ( itSortedHarq==sortedUlHarqRetx.begin() )
            {
              done = true;
              break;
            }
      }

      m_ulHarqInfoList.clear ();
      m_ulHarqInfoList = ulInfoListUntxed;

      //If we did not end pointing to a new layer-symbol slot, update the highest next sym available accross all layers
      //all symbols in the remaining layer-symbol 2D rectangle are "wasted" because we leave them empty
      if ( layerIdx > 0 )
        {
          symAvail -= ( m_phyMacConfig->GetNumEnbLayers () - layerIdx ) * ( lastSymAvail - futureLastSymAvail );
          for (uint8_t i=layerIdx; i < m_phyMacConfig->GetNumEnbLayers () ; i++)
            {
              symAvailLayer[i] -= ( lastSymAvail - futureLastSymAvail );
            }
          layerIdx = 0;
          lastSymAvail = futureLastSymAvail;
        }
    }
  // ********************* END OF HARQ SECTION, START OF NEW DATA SCHEDULING ********************* //

//  //temporary code only while the harq part is done and the rest is not
//  int32_t maxNextSymAvailLayer = nextSymAvail;
//  int32_t minLastSymAvailLayer = lastSymAvail;
//  for (uint8_t i=layerIdx; i < m_phyMacConfig->GetNumEnbLayers () ; i++){
//      nextSymAvailLayer[i] = nextSymAvail;
//      lastSymAvailLayer[i] = lastSymAvail;
//  }

  // get info on active DL flows
  if (symAvail > 0 && !m_ulOnly)        // remaining symbols in current subframe after HARQ retx sched
    {
      for (itRlcBuf = m_rlcBufferReq.begin (); itRlcBuf != m_rlcBufferReq.end (); itRlcBuf++)
        {
          itUeInfo = ueInfo.find (itRlcBuf->m_rnti);
          //		if (itUeInfo != ueInfo.end () && itUeInfo->second.m_dlSymbols > 0)
          //		{
          //			continue;
          //		}

          if ( (((*itRlcBuf).m_rlcTransmissionQueueSize > 0)
                || ((*itRlcBuf).m_rlcRetransmissionQueueSize > 0)
                || ((*itRlcBuf).m_rlcStatusPduSize > 0)) )
            {
              NS_LOG_INFO (this << " User " << itRlcBuf->m_rnti << " LC " << (uint16_t)itRlcBuf->m_logicalChannelIdentity << " is active, status  " << (*itRlcBuf).m_rlcStatusPduSize << " retx " << (*itRlcBuf).m_rlcRetransmissionQueueSize << " tx " << (*itRlcBuf).m_rlcTransmissionQueueSize);
              std::map <uint16_t,uint8_t>::iterator itCqi = m_wbCqiRxed.find (itRlcBuf->m_rnti);
              uint8_t cqi = 0;
              if (itCqi != m_wbCqiRxed.end ())
                {
                  cqi = itCqi->second;
                }
              else                   // no CQI available
                {
                  NS_LOG_INFO (this << " UE " << itRlcBuf->m_rnti << " does not have DL-CQI");
                  cqi = 1;                       // lowest value for trying a transmission
                }
              if (cqi != 0 || m_fixedMcsDl)                     // CQI == 0 means "out of range" (see table 7.2.3-1 of 36.213)
                {
                  // IAB: check if it is a relay or a UE
                  bool isIab = false;
                  if(m_rntiIabInfoMap.find(itRlcBuf->m_rnti) != m_rntiIabInfoMap.end())
                  {
                    isIab = m_rntiIabInfoMap.find(itRlcBuf->m_rnti)->second.first;
                  }

                  if (itUeInfo == ueInfo.end ())
                    {
                      nFlowsDl++;                            // for simplicity, all RLC LCs are considered as a single flow
                      if(!isIab)
                      {
                        nFlowsAccessDl++;
                      }
                      else
                      {
                        nFlowsBackhaulDl++;
                      }
                      itUeInfo = ueInfo.insert (std::pair<uint16_t, struct UeSchedInfo> (itRlcBuf->m_rnti, UeSchedInfo () )).first;
                    }
                  else if (itUeInfo->second.m_maxDlBufSize == 0)
                    {
                      nFlowsDl++;
                      if(!isIab)
                      {
                        nFlowsAccessDl++;
                      }
                      else
                      {
                        nFlowsBackhaulDl++;
                      }
                    }
                  itUeInfo->second.m_iab = isIab;

                  if (m_fixedMcsDl)
                    {
                      itUeInfo->second.m_dlMcs = m_mcsDefaultDl;
                    }
                  else
                    {
                      itUeInfo->second.m_dlMcs = m_amc->GetMcsFromCqi (cqi);                            // get MCS
                    }

                  // temporarily store the TX queue size
                  if (itRlcBuf->m_rlcStatusPduSize > 0)
                    {
                      RlcPduInfo newRlcStatusPdu;
                      newRlcStatusPdu.m_lcid = itRlcBuf->m_logicalChannelIdentity;
                      newRlcStatusPdu.m_size += itRlcBuf->m_rlcStatusPduSize + m_subHdrSize;
                      itUeInfo->second.m_rlcPduInfo.push_back (newRlcStatusPdu);
                      itUeInfo->second.m_maxDlBufSize += newRlcStatusPdu.m_size;                            // add to total DL buffer size
                    }

                  RlcPduInfo newRlcEl;
                  newRlcEl.m_lcid = itRlcBuf->m_logicalChannelIdentity;
                  if (itRlcBuf->m_rlcRetransmissionQueueSize > 0)
                    {
                      newRlcEl.m_size = itRlcBuf->m_rlcRetransmissionQueueSize;
                    }
                  else if (itRlcBuf->m_rlcTransmissionQueueSize > 0)
                    {
                      newRlcEl.m_size = itRlcBuf->m_rlcTransmissionQueueSize;
                    }

                  if (newRlcEl.m_size > 0)
                    {
                      if (newRlcEl.m_size < 8)
                        {
                          newRlcEl.m_size = 8;
                        }
                      newRlcEl.m_size += m_rlcHdrSize + m_subHdrSize + 10;
                      itUeInfo->second.m_rlcPduInfo.push_back (newRlcEl);
                      itUeInfo->second.m_maxDlBufSize += newRlcEl.m_size;                            // add to total DL buffer size
                    }
                }
              else
                {                 // SINR out of range, don't schedule for DL
                  NS_LOG_INFO ("*** RNTI " << itRlcBuf->m_rnti << " DL-CQI out of range, skipping allocation");
                }
            }
        }
    }

  // get info on active UL flows
  if (symAvail > 0 && !m_dlOnly)        // remaining symbols in future UL subframe after HARQ retx sched
    {
      std::map <uint16_t,uint32_t>::iterator ceBsrIt;
      for (ceBsrIt = m_ceBsrRxed.begin (); ceBsrIt != m_ceBsrRxed.end (); ceBsrIt++)
        {
          if (ceBsrIt->second > 0)                // UL buffer size > 0
            {
              std::map <uint16_t, struct UlCqiMapElem>::iterator itCqi = m_ueUlCqi.find (ceBsrIt->first);
              int cqi = 0;
              int mcs = 0;
              if (itCqi == m_ueUlCqi.end ())                   // no cqi info for this UE
                {
                  NS_LOG_INFO (this << " UE " << ceBsrIt->first << " does not have UL-CQI");
                  cqi = 1;
                  mcs = 0;
                }
              else
                {
                  cqi = 0;
                  SpectrumValue specVals (MmWaveSpectrumValueHelper::GetSpectrumModel (m_phyMacConfig));
                  Values::iterator specIt = specVals.ValuesBegin ();
                  for (unsigned ichunk = 0; ichunk < m_phyMacConfig->GetTotalNumChunk (); ichunk++)
                    {
                      //double sinrLin = std::pow (10, itCqi->second.m_ueUlCqi.at (ichunk) / 10);
//						double se1 = log2 ( 1 + (std::pow (10, sinrLin / 10 )  /
//								( (-std::log (5.0 * m_berDl )) / 1.5) ));
//						cqi += m_amc->GetCqiFromSpectralEfficiency (se1);
                      NS_ASSERT (specIt != specVals.ValuesEnd ());
                      *specIt = itCqi->second.m_ueUlCqi.at (ichunk);                           //sinrLin;
                      specIt++;
                    }

                  cqi = m_amc->CreateCqiFeedbackWbTdma (specVals, itCqi->second.m_numSym, itCqi->second.m_tbSize, mcs);
//					for (unsigned i = 0; i < chunkCqi.size(); i++)
//					{
//						cqi += chunkCqi[i];
//					}
//					cqi = cqi / m_phyMacConfig->GetTotalNumChunk ();

                  // take the lowest CQI value (worst chunk)
                  //				double minSinr = itCqi->second.at (0);
                  //				double sinrLinAvg = std::pow (10, itCqi->second.at (0) / 10);
                  //				for (unsigned ichunk = 1; ichunk < m_phyMacConfig->GetTotalNumChunk (); ichunk++)
                  //				{
                  //					if (itCqi->second.at (ichunk) < minSinr)
                  //					{
                  //						minSinr = itCqi->second.at (ichunk);
                  //					}
                  //					sinrLinAvg += std::pow (10, itCqi->second.at (ichunk) / 10);
                  //				}
                  //				// TODO: verify SE calculation
                  //				sinrLinAvg /= m_phyMacConfig->GetTotalNumChunk ();
                  ////				double se = log2 ( 1 + sinrLinAvg );
                  //				double se = log2 ( 1 + (std::pow (10, minSinr / 10 )  /
                  //						( (-std::log (5.0 * 0.00005 )) / 1.5) ));
                  //				cqi = m_amc->GetCqiFromSpectralEfficiency (se);
                  if (cqi == 0 && !m_fixedMcsUl)                       // out of range (SINR too low)
                    {
                      NS_LOG_INFO ("*** RNTI " << ceBsrIt->first << " UL-CQI out of range, skipping allocation in UL");
                      break;                            // do not allocate UE in uplink
                    }
                }
              // IAB: check if it is a relay or a UE
              bool isIab = false;
              if(m_rntiIabInfoMap.find(ceBsrIt->first) != m_rntiIabInfoMap.end())
              {
                isIab = m_rntiIabInfoMap.find(ceBsrIt->first)->second.first;
              }

              itUeInfo = ueInfo.find (ceBsrIt->first);
              if (itUeInfo == ueInfo.end ())
                {
                  itUeInfo = ueInfo.insert (std::pair<uint16_t, struct UeSchedInfo> (ceBsrIt->first, UeSchedInfo () )).first;
                  nFlowsUl++;
                  if(!isIab)
                  {
                    nFlowsAccessUl++;
                  }
                  else
                  {
                    nFlowsBackhaulUl++;
                  }
                }
              else if (itUeInfo->second.m_maxUlBufSize == 0)
                {
                  nFlowsUl++;
                  if(!isIab)
                  {
                    nFlowsAccessUl++;
                  }
                  else
                  {
                    nFlowsBackhaulUl++;
                  }
                }
              itUeInfo->second.m_iab = isIab;
              if (m_fixedMcsUl)
                {
                  itUeInfo->second.m_ulMcs = m_mcsDefaultUl;
                }
              else
                {
                  itUeInfo->second.m_ulMcs = mcs;                      //m_amc->GetMcsFromCqi (cqi);  // get MCS
                }
              itUeInfo->second.m_maxUlBufSize = ceBsrIt->second + m_rlcHdrSize + m_macHdrSize + 8;
            }
        }
    }

  NS_LOG_DEBUG(this << " nFlowsDl " << nFlowsDl << " nFlowsUl " << nFlowsUl 
    << " nFlowsAccessDl " << nFlowsAccessDl << " nFlowsAccessUl " << nFlowsAccessUl
    << " nFlowsBackhaulDl " << nFlowsBackhaulDl << " nFlowBackhaulsUl " << nFlowsBackhaulUl);
  WriteLogToFile("nFlowsDl " + std::to_string(nFlowsDl) + " nFlowsUl " + std::to_string(nFlowsUl) 
    + " nFlowsAccessDl " + std::to_string(nFlowsAccessDl) + " nFlowsAccessUl " + std::to_string(nFlowsAccessUl)
    + " nFlowsBackhaulDl " + std::to_string(nFlowsBackhaulDl) + " nFlowBackhaulsUl " + std::to_string(nFlowsBackhaulUl));

  if (ueInfo.size () > 0)
    {
      // compute requested num slots and TB size based on MCS and DL buffer size
      // final allocated slots may be less
      int totDlSymReq = 0;
      int totUlSymReq = 0;
      int totAccessDlSymReq = 0;
      int totAccessUlSymReq = 0;
      int totBackhaulDlSymReq = 0;
      int totBackhaulUlSymReq = 0;

      for (itUeInfo = ueInfo.begin (); itUeInfo != ueInfo.end (); itUeInfo++)
        {
          unsigned dlTbSize = 0;
          unsigned ulTbSize = 0;
          if (itUeInfo->second.m_maxDlBufSize > 0)
            {
              itUeInfo->second.m_maxDlSymbols = CalcMinTbSizeNumSym (itUeInfo->second.m_dlMcs, itUeInfo->second.m_maxDlBufSize, dlTbSize);
              itUeInfo->second.m_maxDlBufSize = dlTbSize;
              if (m_fixedTti)
                {
                  itUeInfo->second.m_maxDlSymbols = ceil ((double)itUeInfo->second.m_maxDlSymbols / (double)m_symPerSlot) * m_symPerSlot;                  // round up to nearest sym per TTI
                }
              totDlSymReq += itUeInfo->second.m_maxDlSymbols;
              NS_LOG_DEBUG("itUeInfo->second.m_iab " << itUeInfo->second.m_iab << " " << itUeInfo->first);
              if(itUeInfo->second.m_iab)
              {
                totBackhaulDlSymReq += itUeInfo->second.m_maxDlSymbols;
              }
              else
              {
                totAccessDlSymReq += itUeInfo->second.m_maxDlSymbols;
              }
            }
          if (itUeInfo->second.m_maxUlBufSize > 0)
            {
              itUeInfo->second.m_maxUlSymbols = CalcMinTbSizeNumSym (itUeInfo->second.m_ulMcs, itUeInfo->second.m_maxUlBufSize + 10, ulTbSize);
              itUeInfo->second.m_maxUlBufSize = ulTbSize;
              if (m_fixedTti)
                {
                  itUeInfo->second.m_maxUlSymbols = ceil ((double)itUeInfo->second.m_maxUlSymbols / (double)m_symPerSlot) * m_symPerSlot;                  // round up to nearest sym per TTI
                }
              totUlSymReq += itUeInfo->second.m_maxUlSymbols;
              NS_LOG_DEBUG("itUeInfo->second.m_iab " << itUeInfo->second.m_iab << " " << itUeInfo->first);
              if(itUeInfo->second.m_iab)
              {
                totBackhaulUlSymReq += itUeInfo->second.m_maxUlSymbols;
              }
              else
              {
                totAccessUlSymReq += itUeInfo->second.m_maxUlSymbols;
              }
            }
        }

      // IAB: limit resources for IAB devices
      int maxSymAvailableForIab = std::floor(symAvail/2);
      int removedSymbolsDl = 0;
      int removedSymbolsUl = 0;
      for(auto itUeInfo = ueInfo.begin(); itUeInfo != ueInfo.end(); ++itUeInfo)
      {
        if(itUeInfo->second.m_iab)
        {
          int totalSymbolsRequested = itUeInfo->second.m_maxUlSymbols + itUeInfo->second.m_maxDlSymbols;
          int oldUlSymbols = itUeInfo->second.m_maxUlSymbols;
          int oldDlSymbols = itUeInfo->second.m_maxDlSymbols;
          if(totalSymbolsRequested > maxSymAvailableForIab)
          {
            double scaleFactor = (double)maxSymAvailableForIab/(double)totalSymbolsRequested;
            NS_LOG_DEBUG("IAB with rnti " << itUeInfo->first << " requests too many symbols " << totalSymbolsRequested << " total available " 
              << symAvail << " for IAB " << maxSymAvailableForIab << " scaleFactor " << scaleFactor);
            if(itUeInfo->second.m_maxUlSymbols > 0)
            {
              int newSymbols = std::max(1, (int)std::floor(scaleFactor*oldUlSymbols));
              removedSymbolsUl += (itUeInfo->second.m_maxUlSymbols - newSymbols);
              itUeInfo->second.m_maxUlSymbols = newSymbols;
            }
            if(itUeInfo->second.m_maxDlSymbols > 0)
            {
              int newSymbols = std::max(1, (int)std::floor(scaleFactor*oldDlSymbols));
              removedSymbolsDl += (itUeInfo->second.m_maxDlSymbols - newSymbols);
              itUeInfo->second.m_maxDlSymbols = newSymbols;
            }
            NS_LOG_DEBUG("New symbols request DL " << (uint32_t)itUeInfo->second.m_maxDlSymbols << 
              " UL " <<(uint32_t)itUeInfo->second.m_maxUlSymbols);
          }
        }
      }

      NS_LOG_DEBUG(this << " totDlSymReq " << totDlSymReq << " totUlSymReq " << totUlSymReq 
        << " totAccessDlSymReq " << totAccessDlSymReq << " totAccessUlSymReq " << totAccessUlSymReq
        << " totBackhaulDlSymReq " << totBackhaulDlSymReq << " totBackhaulUlSymReq " << totBackhaulUlSymReq);
      WriteLogToFile("totDlSymReq " + std::to_string(totDlSymReq) + " totUlSymReq " + std::to_string(totUlSymReq) 
        + " totAccessDlSymReq " + std::to_string(totAccessDlSymReq) + " totAccessUlSymReq " + std::to_string(totAccessUlSymReq)
        + " totBackhaulDlSymReq " + std::to_string(totBackhaulDlSymReq) + " totBackhaulUlSymReq " + std::to_string(totBackhaulUlSymReq));
      
      // Display final flow counts
      NS_LOG_DEBUG("=== FINAL FLOW COUNTS ===");
      NS_LOG_DEBUG("DL Flows - Total: " << nFlowsDl << " Access: " << nFlowsAccessDl << " Backhaul: " << nFlowsBackhaulDl);
      NS_LOG_DEBUG("UL Flows - Total: " << nFlowsUl << " Access: " << nFlowsAccessUl << " Backhaul: " << nFlowsBackhaulUl);
      NS_LOG_DEBUG("========================");
      WriteLogToFile("=== FINAL FLOW COUNTS ===");
      WriteLogToFile("DL Flows - Total: " + std::to_string(nFlowsDl) + " Access: " + std::to_string(nFlowsAccessDl) + " Backhaul: " + std::to_string(nFlowsBackhaulDl));
      WriteLogToFile("UL Flows - Total: " + std::to_string(nFlowsUl) + " Access: " + std::to_string(nFlowsAccessUl) + " Backhaul: " + std::to_string(nFlowsBackhaulUl));
      WriteLogToFile("========================");

      totDlSymReq -= removedSymbolsDl;
      totUlSymReq -= removedSymbolsUl;
      totBackhaulDlSymReq -= removedSymbolsDl;
      totBackhaulUlSymReq -= removedSymbolsUl;

      NS_LOG_DEBUG(this << " after totDlSymReq " << totDlSymReq << " totUlSymReq " << totUlSymReq 
        << " totAccessDlSymReq " << totAccessDlSymReq << " totAccessUlSymReq " << totAccessUlSymReq
        << " totBackhaulDlSymReq " << totBackhaulDlSymReq << " totBackhaulUlSymReq " << totBackhaulUlSymReq);
      WriteLogToFile("after totDlSymReq " + std::to_string(totDlSymReq) + " totUlSymReq " + std::to_string(totUlSymReq) 
        + " totAccessDlSymReq " + std::to_string(totAccessDlSymReq) + " totAccessUlSymReq " + std::to_string(totAccessUlSymReq)
        + " totBackhaulDlSymReq " + std::to_string(totBackhaulDlSymReq) + " totBackhaulUlSymReq " + std::to_string(totBackhaulUlSymReq));

      //Divide the HBF frame in DL and UL contiguous regions proportionally to the total demand for new TB allocations.
      uint32_t firstUlSymbol = nextSymAvail + round ( ((double) lastSymAvail - (double) nextSymAvail +1 ) * ( (double) totDlSymReq) / ( (double) totDlSymReq + (double) totUlSymReq) );

      NS_LOG_LOGIC("Semiempty frame after HARQ alloc: Available symbols " << nextSymAvail << " to " << lastSymAvail << " UL start in symbol "<<firstUlSymbol);
      WriteLogToFile("Semiempty frame after HARQ alloc: Available symbols " + std::to_string(nextSymAvail) + " to " + std::to_string(lastSymAvail) + " UL start in symbol " + std::to_string(firstUlSymbol));

      //Run a (almost always) RR allocator on each Layer separately for the each DL UL portions

      //DL allocation part (we follow a RR policy without looking at buffer size, potentially wasting some resources when transmit buffers are short)
      uint8_t nDlFlowsPerLayer = ceil( ( (double ) nFlowsDl ) / ( (double ) m_phyMacConfig->GetNumEnbLayers () ) );
      uint8_t nDlSymPerLayer = firstUlSymbol - nextSymAvail;
      uint8_t symPerDlBlock = floor( ( (double ) nDlSymPerLayer ) / ( (double ) nDlFlowsPerLayer ) );
      if (symPerDlBlock==0)
        {//if there are more UE than symbols not all ue get one and RR serves as many UE as possible
          symPerDlBlock=1;
        }

      if (m_fixedTti)
        {
          symPerDlBlock = ceil ((double)symPerDlBlock / (double)m_symPerSlot) * m_symPerSlot;
        }
      NS_LOG_LOGIC("RR division of DL region: "<< (int)nDlFlowsPerLayer <<" groups up to "<< (int)m_phyMacConfig->GetNumEnbLayers () <<" UE get up to " << (int)symPerDlBlock << " symbols each");
      WriteLogToFile("RR division of DL region: " + std::to_string((int)nDlFlowsPerLayer) + " groups up to " + std::to_string((int)m_phyMacConfig->GetNumEnbLayers()) + " UE get up to " + std::to_string((int)symPerDlBlock) + " symbols each");


      //UL allocation part
      uint8_t nUlFlowsPerLayer = ceil( ( (double ) nFlowsUl ) / ( (double ) m_phyMacConfig->GetNumEnbLayers () ) );
      uint8_t nUlSymPerLayer = lastSymAvail - firstUlSymbol + 1;
      uint8_t symPerUlBlock = floor( ( (double ) nUlSymPerLayer ) / ( (double ) nUlFlowsPerLayer ) );
      if (symPerUlBlock==0)
        {//if there are more UE than symbols not all ue get one and RR serves as many UE as possible
          symPerUlBlock=1;
        }
      if (m_fixedTti)
        {
          symPerUlBlock = ceil ((double)symPerUlBlock / (double)m_symPerSlot) * m_symPerSlot;
        }
      NS_LOG_LOGIC("RR division of UL region: "<< (int)nUlFlowsPerLayer <<" groups up to "<< (int)m_phyMacConfig->GetNumEnbLayers () <<" UE get up to " << (int)symPerUlBlock << " symbols each");
      WriteLogToFile("RR division of UL region: " + std::to_string((int)nUlFlowsPerLayer) + " groups up to " + std::to_string((int)m_phyMacConfig->GetNumEnbLayers()) + " UE get up to " + std::to_string((int)symPerUlBlock) + " symbols each");

      //recover the last UE that was served
      std::map <uint16_t, struct UeSchedInfo>::iterator itUeInfoStart;
      if (m_nextRnti != 0)          // start with RNTI at which the scheduler left off
        {
          itUeInfoStart = ueInfo.find (m_nextRnti);
          if (itUeInfoStart == ueInfo.end ())
            {
              itUeInfoStart = ueInfo.begin ();
            }
        }
      else          // start with first active RNTI
        {
          itUeInfoStart = ueInfo.begin ();
        }
      itUeInfo = itUeInfoStart; // pick up the RR from the last user served

      // FIX: Reset allocation flags for all UEs at the beginning of scheduling cycle
      for (auto& uePair : ueInfo) {
        uePair.second.m_dlAllocDone = false;
        uePair.second.m_ulAllocDone = false;
      }

      uint8_t blockIdxDl = 0;
      uint8_t layerIdxDl = 0;
      uint8_t blockIdxUl = 0;
      uint8_t layerIdxUl = 0;
      
      // NEW APPROACH: Buffer-based priority scheduling instead of pure Round Robin
      // Phase 1: Allocate DL for all UEs (including IAB) with buffer-based priority
      // Phase 2: Allocate UL for all UEs (including IAB) after DL allocation is complete
      
      // Display the UeInfo list with the amount of data each user has
      NS_LOG_DEBUG("=== UE Buffer Status List ===");
      WriteLogToFile("=== UE Buffer Status List ===");
      for (const auto& uePair : ueInfo) {
        NS_LOG_DEBUG("  RNTI: " << uePair.first
                      << "  maxDlBufSize: " << uePair.second.m_maxDlBufSize
                      << "  maxUlBufSize: " << uePair.second.m_maxUlBufSize
                      << "  IAB: " << uePair.second.m_iab);
        WriteLogToFile("  RNTI: " + std::to_string(uePair.first)
                      + "  maxDlBufSize: " + std::to_string(uePair.second.m_maxDlBufSize)
                      + "  maxUlBufSize: " + std::to_string(uePair.second.m_maxUlBufSize)
                      + "  IAB: " + std::to_string(uePair.second.m_iab));
      }
      NS_LOG_DEBUG("============================");
      WriteLogToFile("============================");
      
      
      // Phase 1: DL Allocation for all UEs (including IAB) with buffer-based priority
      NS_LOG_DEBUG("*** PHASE 1: DL ALLOCATION START ***");
      WriteLogToFile("*** PHASE 1: DL ALLOCATION START ***");
      
      // Create a priority queue based on buffer size to prevent buffer overflow
      std::vector<std::pair<uint16_t, uint32_t>> uePriorityList;
      for (auto& uePair : ueInfo) {
        if (!uePair.second.m_dlAllocDone && uePair.second.m_maxDlBufSize > 0) {
          uePriorityList.push_back(std::make_pair(uePair.first, uePair.second.m_maxDlBufSize));
        }
      }
      
      // Display buffer list before sorting
      NS_LOG_DEBUG("=== BUFFER LIST BEFORE SORTING ===");
      WriteLogToFile("=== BUFFER LIST BEFORE SORTING ===");
      for (const auto& uePair : uePriorityList) {
        NS_LOG_DEBUG("  RNTI: " << uePair.first << " Buffer Size: " << uePair.second);
        WriteLogToFile("  RNTI: " + std::to_string(uePair.first) + " Buffer Size: " + std::to_string(uePair.second));
      }
      NS_LOG_DEBUG("=================================");
      WriteLogToFile("=================================");
      
      // // Sort by buffer size (largest first) to prioritize UEs with buffer overflow
      // std::sort(uePriorityList.begin(), uePriorityList.end(), 
      //           [](const std::pair<uint16_t, uint32_t>& a, const std::pair<uint16_t, uint32_t>& b) {
      //             return a.second > b.second; // Sort by buffer size descending
      //           });
      
      // Display buffer list after sorting
      NS_LOG_DEBUG("=== BUFFER LIST AFTER SORTING ===");
      WriteLogToFile("=== BUFFER LIST AFTER SORTING ===");
      for (const auto& uePair : uePriorityList) {
        NS_LOG_DEBUG("  RNTI: " << uePair.first << " Buffer Size: " << uePair.second);
        WriteLogToFile("  RNTI: " + std::to_string(uePair.first) + " Buffer Size: " + std::to_string(uePair.second));
      }
      NS_LOG_DEBUG("==================================");
      WriteLogToFile("==================================");
      
      bool dlPhaseDone = false;
      size_t priorityIndex = 0;
      
      while (!dlPhaseDone && priorityIndex < uePriorityList.size()) {
        uint16_t currentRnti = uePriorityList[priorityIndex].first;
        auto itUeInfoDl = ueInfo.find(currentRnti);
        
        if (itUeInfoDl == ueInfo.end()) {
          priorityIndex++;
          continue;
        }
        
        UeSchedInfo &ueSchedInfo = itUeInfoDl->second;
        
        // Allocate DL for all UEs (including IAB) with buffer-based priority
        if (!ueSchedInfo.m_dlAllocDone && ueSchedInfo.m_maxDlBufSize > 0) {
          NS_LOG_DEBUG("*** UE HAS DATA: RNTI=" << currentRnti 
                        << " IAB=" << ueSchedInfo.m_iab 
                        << " maxDlBufSize=" << ueSchedInfo.m_maxDlBufSize 
                        << " at time " << Simulator::Now().GetSeconds() << "s");
          WriteLogToFile("*** UE HAS DATA: RNTI=" + std::to_string(currentRnti) 
                        + " IAB=" + std::to_string(ueSchedInfo.m_iab) 
                        + " maxDlBufSize=" + std::to_string(ueSchedInfo.m_maxDlBufSize) 
                        + " at time " + std::to_string(Simulator::Now().GetSeconds()) + "s");
        } else {
          NS_LOG_DEBUG("*** UE NO DATA: RNTI=" << currentRnti 
                        << " IAB=" << ueSchedInfo.m_iab 
                        << " dlAllocDone=" << ueSchedInfo.m_dlAllocDone 
                        << " maxDlBufSize=" << ueSchedInfo.m_maxDlBufSize 
                        << " at time " << Simulator::Now().GetSeconds() << "s");
          WriteLogToFile("*** UE NO DATA: RNTI=" + std::to_string(currentRnti) 
                        + " IAB=" + std::to_string(ueSchedInfo.m_iab) 
                        + " dlAllocDone=" + std::to_string(ueSchedInfo.m_dlAllocDone) 
                        + " maxDlBufSize=" + std::to_string(ueSchedInfo.m_maxDlBufSize) 
                        + " at time " + std::to_string(Simulator::Now().GetSeconds()) + "s");
        }
        
        if (!ueSchedInfo.m_dlAllocDone && ueSchedInfo.m_maxDlBufSize > 0) {
          ueSchedInfo.m_dlSymbols = std::min(symPerDlBlock, ueSchedInfo.m_maxDlSymbols);
          ueSchedInfo.m_ulSymbols = 0; // No UL allocation in DL phase
          
          NS_LOG_DEBUG("*** SCHEDULING DECISION: UE RNTI=" << currentRnti 
                        << " IAB=" << ueSchedInfo.m_iab 
                        << " allocated DL=" << (int)ueSchedInfo.m_dlSymbols 
                        << " UL=" << (int)ueSchedInfo.m_ulSymbols 
                        << " Layer=" << (int)layerIdxDl
                        << " Block=" << (int)blockIdxDl
                        << " at time " << Simulator::Now().GetSeconds() << "s");
          WriteLogToFile("*** SCHEDULING DECISION: UE RNTI=" + std::to_string(currentRnti) 
                        + " IAB=" + std::to_string(ueSchedInfo.m_iab) 
                        + " allocated DL=" + std::to_string((int)ueSchedInfo.m_dlSymbols) 
                        + " UL=" + std::to_string((int)ueSchedInfo.m_ulSymbols) 
                        + " Layer=" + std::to_string((int)layerIdxDl)
                        + " Block=" + std::to_string((int)blockIdxDl)
                        + " at time " + std::to_string(Simulator::Now().GetSeconds()) + "s");
          
          // Choose DL start symbol avoiding IAB busy symbols if split mode is active
          uint32_t allocStartDl = nextSymAvail;
          if (ueSchedInfo.m_dlSymbols > 0 && m_split && CheckOverlapWithBusyResources(allocStartDl, ueSchedInfo.m_dlSymbols, layerIdxDl)) 
          {
            uint8_t tmp = static_cast<uint8_t>(nextSymAvail);
            bool found = false;
            while ((uint32_t)tmp + ueSchedInfo.m_dlSymbols <= firstUlSymbol) {
              int freeCnt = GetNumFreeSymbols(tmp, ueSchedInfo.m_dlSymbols);
              if (freeCnt >= (int)ueSchedInfo.m_dlSymbols) { allocStartDl = tmp; found = true; break; }
              tmp = GetFirstFreeSymbol(tmp, std::max(0, freeCnt));
              if ((uint32_t)tmp + ueSchedInfo.m_dlSymbols > firstUlSymbol) break;
            }
            if (!found) {
              // No room without overlap in DL region for this UE in this round
              ueSchedInfo.m_dlAllocDone = true;
              priorityIndex++;
              if (priorityIndex >= uePriorityList.size()) { dlPhaseDone = true; }
              continue;
            }
          }

          if (ueSchedInfo.m_dlSymbols > 0 && (allocStartDl + ueSchedInfo.m_dlSymbols) <= firstUlSymbol) {
            NS_ASSERT(symAvail >= symPerDlBlock);
            NS_ASSERT(symAvailLayer[layerIdxDl] >= symPerDlBlock);
            symAvail -= symPerDlBlock;
            symAvailLayer[layerIdxDl] -= symPerDlBlock;

            DciInfoElementTdma dci;
            dci.m_rnti = currentRnti;
            dci.m_format = 0;
            dci.m_layerInd = layerIdxDl;
            dci.m_symStart = static_cast<uint8_t>(allocStartDl);
            dci.m_numSym = ueSchedInfo.m_dlSymbols;
            dci.m_ndi = 1;
            dci.m_mcs = ueSchedInfo.m_dlMcs;
            dci.m_tbSize = m_amc->GetTbSizeFromMcsSymbols(dci.m_mcs, dci.m_numSym) / 8;
            dci.m_rv = 0;
            dci.m_harqProcess = UpdateDlHarqProcessId(currentRnti);
            NS_ASSERT(dci.m_harqProcess < m_phyMacConfig->GetNumHarqProcess());
            NS_LOG_LOGIC("UE " << currentRnti << " DL harqId " << (unsigned)dci.m_harqProcess << " HARQ process assigned");

            SlotAllocInfo slotInfo(tempDlslotIdx++, SlotAllocInfo::DL_slotAllocInfo, SlotAllocInfo::CTRL_DATA, SlotAllocInfo::DIGITAL, currentRnti, layerIdxDl);
            slotInfo.m_dci = dci;
                          NS_LOG_LOGIC("UE " << dci.m_rnti << " gets DL slots " << (unsigned)dci.m_symStart << "-" << (unsigned)(dci.m_symStart + dci.m_numSym - 1) <<
                          " tbs " << dci.m_tbSize << " mcs " << (unsigned)dci.m_mcs << " harqId " << (unsigned)dci.m_harqProcess << " rv " << (unsigned)dci.m_rv <<
                          " in frame " << ret.m_sfnSf.m_frameNum << " subframe " << (unsigned)ret.m_sfnSf.m_sfNum << " layer " << (unsigned)dci.m_layerInd);

            if (m_harqOn == true) {
              // store DCI for HARQ buffer
              std::map <uint16_t, DlHarqProcessesDciInfoList_t>::iterator itDciInfo = m_dlHarqProcessesDciInfoMap.find(dci.m_rnti);
              if (itDciInfo == m_dlHarqProcessesDciInfoMap.end()) {
                NS_FATAL_ERROR("Unable to find RNTI entry in DCI HARQ buffer for RNTI " << dci.m_rnti);
              }
              (*itDciInfo).second.at(dci.m_harqProcess) = dci;
              // refresh timer
              std::map <uint16_t, DlHarqProcessesTimer_t>::iterator itHarqTimer = m_dlHarqProcessesTimer.find(dci.m_rnti);
              if (itHarqTimer == m_dlHarqProcessesTimer.end()) {
                NS_FATAL_ERROR("Unable to find HARQ timer for RNTI " << (uint16_t)dci.m_rnti);
              }
              (*itHarqTimer).second.at(dci.m_harqProcess) = 0;
            }

                         // distribute bytes between active RLC queues
             unsigned numLc = ueSchedInfo.m_rlcPduInfo.size();
             unsigned bytesRem = dci.m_tbSize;
             unsigned numFulfilled = 0;
             uint16_t avgPduSize = bytesRem / numLc;
             // first for loop computes extra to add to average if some flows are less than average
             for (unsigned i = 0; i < ueSchedInfo.m_rlcPduInfo.size(); i++) {
               if (ueSchedInfo.m_rlcPduInfo[i].m_size < avgPduSize) {
                 bytesRem -= ueSchedInfo.m_rlcPduInfo[i].m_size;
                 numFulfilled++;
               }
             }
             
             if (numFulfilled < ueSchedInfo.m_rlcPduInfo.size()) {
               avgPduSize = bytesRem / (ueSchedInfo.m_rlcPduInfo.size() - numFulfilled);
             }
             
             for (unsigned i = 0; i < ueSchedInfo.m_rlcPduInfo.size(); i++) {
               if (ueSchedInfo.m_rlcPduInfo[i].m_size > avgPduSize) {
                 ueSchedInfo.m_rlcPduInfo[i].m_size = avgPduSize;
               }
               // else tbSize equals RLC queue size
               NS_ASSERT(ueSchedInfo.m_rlcPduInfo[i].m_size > 0);
               // update RLC buffer info with expected queue size after scheduling
              UpdateDlRlcBufferInfo(currentRnti, ueSchedInfo.m_rlcPduInfo[i].m_lcid, ueSchedInfo.m_rlcPduInfo[i].m_size - m_subHdrSize);
               slotInfo.m_rlcPduInfo.push_back(ueSchedInfo.m_rlcPduInfo[i]);
               if (m_harqOn == true) {
                 // store RLC PDU list for HARQ
                 std::map <uint16_t, DlHarqRlcPduList_t>::iterator itRlcPdu = m_dlHarqProcessesRlcPduMap.find(dci.m_rnti);
                 if (itRlcPdu == m_dlHarqProcessesRlcPduMap.end()) {
                   NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI " << dci.m_rnti);
                 }
                 (*itRlcPdu).second.at(dci.m_harqProcess).push_back(ueSchedInfo.m_rlcPduInfo[i]);
               }
             }

                         tempDlslotAllocInfo[layerIdxDl].push_back(slotInfo);
             ret.m_sfAllocInfo.m_numSymAlloc += dci.m_numSym;
             
             // Update allocation flags after DL allocation
             ueSchedInfo.m_dlAllocDone = true;
             ueSchedInfo.m_allocUlLast = false;
            nextSymAvail = std::max(nextSymAvail, (uint32_t)dci.m_symStart) + symPerDlBlock;
            layerIdxDl++;
            if (layerIdxDl == m_phyMacConfig->GetNumEnbLayers()) {
              layerIdxDl = 0;
              blockIdxDl++;
              NS_ASSERT(blockIdxDl <= nDlFlowsPerLayer);
            }
          } else {
            ueSchedInfo.m_dlAllocDone = true; // Mark as done even if no allocation
          }
        } else {
          ueSchedInfo.m_dlAllocDone = true; // Mark as done if no buffer or already done
        }

        priorityIndex++; // Move to next UE in priority list
        if (priorityIndex >= uePriorityList.size()) {
          dlPhaseDone = true;
        }
      }
      NS_LOG_DEBUG("*** PHASE 1: DL ALLOCATION COMPLETE ***");
      WriteLogToFile("*** PHASE 1: DL ALLOCATION COMPLETE ***");
      
      // Display final DL flow allocation summary
      NS_LOG_DEBUG("=== DL ALLOCATION SUMMARY ===");
      NS_LOG_DEBUG("DL Flows Allocated - Total: " << nFlowsDl << " Access: " << nFlowsAccessDl << " Backhaul: " << nFlowsBackhaulDl);
      NS_LOG_DEBUG("DL Symbols Per Block: " << (int)symPerDlBlock << " Total DL Symbols: " << (int)nDlSymPerLayer);
      NS_LOG_DEBUG("=============================");
      WriteLogToFile("=== DL ALLOCATION SUMMARY ===");
      WriteLogToFile("DL Flows Allocated - Total: " + std::to_string(nFlowsDl) + " Access: " + std::to_string(nFlowsAccessDl) + " Backhaul: " + std::to_string(nFlowsBackhaulDl));
      WriteLogToFile("DL Symbols Per Block: " + std::to_string((int)symPerDlBlock) + " Total DL Symbols: " + std::to_string((int)nDlSymPerLayer));
      WriteLogToFile("=============================");

      // Phase 2: UL Allocation for all UEs (including IAB) after DL allocation
      NS_LOG_DEBUG("*** PHASE 2: UL ALLOCATION START ***");
      WriteLogToFile("*** PHASE 2: UL ALLOCATION START ***");
      std::map <uint16_t, struct UeSchedInfo>::iterator itUeInfoUl = itUeInfoStart;
      bool ulPhaseDone = false;
      
      while (!ulPhaseDone) {
        UeSchedInfo &ueSchedInfo = itUeInfoUl->second;
        
        // Allocate UL for all UEs (including IAB) after DL allocation is complete
        if (!ueSchedInfo.m_ulAllocDone && ueSchedInfo.m_maxUlBufSize > 0) {
          ueSchedInfo.m_ulSymbols = std::min(symPerUlBlock, ueSchedInfo.m_maxUlSymbols);
          
          NS_LOG_DEBUG("*** UL PHASE: UE " << itUeInfoUl->first 
                        << " IAB: " << ueSchedInfo.m_iab 
                        << " allocated DL: " << (int)ueSchedInfo.m_dlSymbols 
                        << " UL: " << (int)ueSchedInfo.m_ulSymbols 
                        << " Layer: " << (int)layerIdxUl
                        << " Block: " << (int)blockIdxUl
                        << " at time " << Simulator::Now().GetSeconds() << "s");
          WriteLogToFile("*** UL PHASE: UE " + std::to_string(itUeInfoUl->first) 
                        + " IAB: " + std::to_string(ueSchedInfo.m_iab) 
                        + " allocated DL: " + std::to_string((int)ueSchedInfo.m_dlSymbols) 
                        + " UL: " + std::to_string((int)ueSchedInfo.m_ulSymbols) 
                        + " Layer: " + std::to_string((int)layerIdxUl)
                        + " Block: " + std::to_string((int)blockIdxUl)
                        + " at time " + std::to_string(Simulator::Now().GetSeconds()) + "s");
          
          // Compute UL start avoiding IAB busy symbols
          uint32_t allocStartUl = (lastSymAvail + 1 >= static_cast<uint32_t>(symPerUlBlock)) ? (lastSymAvail + 1 - static_cast<uint32_t>(symPerUlBlock)) : firstUlSymbol;
          if (ueSchedInfo.m_ulSymbols > 0 && m_split && CheckOverlapWithBusyResources(allocStartUl, ueSchedInfo.m_ulSymbols, layerIdxUl)) {
            // Scan upward within the UL region to find a contiguous free window
            uint8_t tmp = static_cast<uint8_t>(firstUlSymbol);
            bool found = false;
            while ((uint32_t)tmp + ueSchedInfo.m_ulSymbols <= (lastSymAvail + 1)) {
              int freeCnt = GetNumFreeSymbols(tmp, ueSchedInfo.m_ulSymbols);
              if (freeCnt >= (int)ueSchedInfo.m_ulSymbols) { allocStartUl = tmp; found = true; break; }
              tmp = GetFirstFreeSymbol(tmp, std::max(0, freeCnt));
              if ((uint32_t)tmp + ueSchedInfo.m_ulSymbols > (lastSymAvail + 1)) break;
            }
            if (!found) {
              ueSchedInfo.m_ulAllocDone = true;
              // advance RR
              itUeInfoUl++;
              if (itUeInfoUl == ueInfo.end()) { itUeInfoUl = ueInfo.begin(); }
              if (itUeInfoUl == itUeInfoStart) { ulPhaseDone = true; }
              continue;
            }
          }

          if (ueSchedInfo.m_ulSymbols > 0 && (allocStartUl >= firstUlSymbol) && ((allocStartUl + ueSchedInfo.m_ulSymbols) <= (lastSymAvail + 1))) {
            DciInfoElementTdma dci;
            dci.m_rnti = itUeInfoUl->first;
            dci.m_format = 1;
            dci.m_layerInd = layerIdxUl;
            dci.m_symStart = static_cast<uint8_t>(allocStartUl);
            dci.m_numSym = ueSchedInfo.m_ulSymbols;
            dci.m_ndi = 1;
            dci.m_mcs = ueSchedInfo.m_ulMcs;
            dci.m_tbSize = m_amc->GetTbSizeFromMcsSymbols(dci.m_mcs, dci.m_numSym) / 8;
            dci.m_rv = 0;
            dci.m_harqProcess = UpdateUlHarqProcessId(itUeInfoUl->first);
            NS_ASSERT(dci.m_harqProcess < m_phyMacConfig->GetNumHarqProcess());
            NS_LOG_LOGIC("UE " << itUeInfoUl->first << " UL harqId " << (unsigned)dci.m_harqProcess << " HARQ process assigned");

            SlotAllocInfo slotInfo(tempUlSlotIdx++, SlotAllocInfo::UL_slotAllocInfo, SlotAllocInfo::CTRL_DATA, SlotAllocInfo::DIGITAL, itUeInfoUl->first, layerIdxUl);
            slotInfo.m_dci = dci;
                          NS_LOG_LOGIC("UE " << dci.m_rnti << " gets UL slots " << (unsigned)dci.m_symStart << "-" << (unsigned)(dci.m_symStart + dci.m_numSym - 1) <<
                          " tbs " << dci.m_tbSize << " mcs " << (unsigned)dci.m_mcs << " harqId " << (unsigned)dci.m_harqProcess << " rv " << (unsigned)dci.m_rv <<
                          " in frame " << ulSchedSfn.m_frameNum << " subframe " << (unsigned)ulSchedSfn.m_sfNum << " layer " << (unsigned)dci.m_layerInd);

            if (m_harqOn == true) {
              uint8_t harqId = dci.m_harqProcess;
              std::map <uint16_t, UlHarqProcessesDciInfoList_t>::iterator itHarqTbInfo = m_ulHarqProcessesDciInfoMap.find(dci.m_rnti);
              if (itHarqTbInfo == m_ulHarqProcessesDciInfoMap.end()) {
                NS_FATAL_ERROR("Unable to find RNTI entry in UL DCI HARQ buffer for RNTI " << dci.m_rnti);
              }
              (*itHarqTbInfo).second.at(harqId) = dci;
              // Update HARQ process status (RV 0)
              std::map <uint16_t, UlHarqProcessesStatus_t>::iterator itStat = m_ulHarqProcessesStatus.find(dci.m_rnti);
              NS_ASSERT(itStat->second[dci.m_harqProcess] > 0);
              // refresh timer
              std::map <uint16_t, UlHarqProcessesTimer_t>::iterator itHarqTimer = m_ulHarqProcessesTimer.find(dci.m_rnti);
              if (itHarqTimer == m_ulHarqProcessesTimer.end()) {
                NS_FATAL_ERROR("Unable to find HARQ timer for RNTI " << (uint16_t)dci.m_rnti);
              }
              (*itHarqTimer).second.at(dci.m_harqProcess) = 0;
            }

            UpdateUlRlcBufferInfo(itUeInfoUl->first, dci.m_tbSize - m_subHdrSize);
            tempUlslotAllocInfo[layerIdxUl].push_front(slotInfo); //remember we fill from end backward
            ret.m_sfAllocInfo.m_numSymAlloc += dci.m_numSym;
            
            // Update allocation flags after UL allocation
            ueSchedInfo.m_ulAllocDone = true;
            ueSchedInfo.m_allocUlLast = true;
            std::vector<uint16_t> ueChunkMap;
            for (unsigned i = 0; i < m_phyMacConfig->GetTotalNumChunk(); i++) {
              ueChunkMap.push_back(dci.m_rnti);
            }
            SfnSf slotSfn = ret.m_sfAllocInfo.m_sfnSf; //this sfnsf struct is used only to tag uplink CQI where the slotNum is not know upon reception so we use the following trick
            slotSfn.m_slotNum = dci.m_symStart * m_phyMacConfig->GetNumEnbLayers() + layerIdxUl;                // use the start symbol index of the slot because the absolute UL slot index depends on the future DL allocation
            // insert into allocation map to recall previous allocations upon receiving UL-CQI
            m_ulAllocationMap.insert(std::pair<uint32_t, struct AllocMapElem>(slotSfn.Encode(), AllocMapElem(ueChunkMap, dci.m_numSym, dci.m_tbSize)));
            layerIdxUl++;
            if (layerIdxUl == m_phyMacConfig->GetNumEnbLayers()) {
              layerIdxUl = 0;
              blockIdxUl++;
              NS_ASSERT(blockIdxUl <= nUlFlowsPerLayer);
              // Move the UL cursor to just before the block we used
              if (allocStartUl > 0) { lastSymAvail = allocStartUl - 1; }
              NS_ASSERT(lastSymAvail + 1 >= firstUlSymbol);
            }
          } else {
            ueSchedInfo.m_ulAllocDone = true; // Mark as done even if no allocation
          }
        } else {
          ueSchedInfo.m_ulAllocDone = true; // Mark as done if no buffer or already done
        }

        itUeInfoUl++; // move to next UE
        if (itUeInfoUl == ueInfo.end()) {
          itUeInfoUl = ueInfo.begin();
        }
        if (itUeInfoUl == itUeInfoStart) {
          ulPhaseDone = true;
        }
      }
      NS_LOG_DEBUG("*** PHASE 2: UL ALLOCATION COMPLETE ***");
      WriteLogToFile("*** PHASE 2: UL ALLOCATION COMPLETE ***");
      
      // Display final UL flow allocation summary
      NS_LOG_DEBUG("=== UL ALLOCATION SUMMARY ===");
      NS_LOG_DEBUG("UL Flows Allocated - Total: " << nFlowsUl << " Access: " << nFlowsAccessUl << " Backhaul: " << nFlowsBackhaulUl);
      NS_LOG_DEBUG("UL Symbols Per Block: " << (int)symPerUlBlock << " Total UL Symbols: " << (int)nUlSymPerLayer);
      NS_LOG_DEBUG("=============================");
      WriteLogToFile("=== UL ALLOCATION SUMMARY ===");
      WriteLogToFile("UL Flows Allocated - Total: " + std::to_string(nFlowsUl) + " Access: " + std::to_string(nFlowsAccessUl) + " Backhaul: " + std::to_string(nFlowsBackhaulUl));
      WriteLogToFile("UL Symbols Per Block: " + std::to_string((int)symPerUlBlock) + " Total UL Symbols: " + std::to_string((int)nUlSymPerLayer));
      WriteLogToFile("=============================");

            // Update the main iterator for compatibility with existing code
      itUeInfo = itUeInfoStart;
      m_nextRnti = itUeInfo->first;
    }

  NS_LOG_INFO ("Fr "<< (int)ret.m_sfnSf.m_frameNum<<" Sf "<<(int)ret.m_sfnSf.m_sfNum <<" DL slot no. "<< 0 << " DL CTRL sym range "<<(int) ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_symStart << " to "<<(int) ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_numSym+(int) ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_symStart-1 << " of " << m_phyMacConfig->GetSymbolsPerSubframe () <<" layerIdx " << (int) ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_layerInd <<" of "<< (int) ret.m_sfAllocInfo.m_numAllocLayers);
  WriteLogToFile("Fr " + std::to_string((int)ret.m_sfnSf.m_frameNum) + " Sf " + std::to_string((int)ret.m_sfnSf.m_sfNum) + " DL slot no. " + std::to_string(0) + " DL CTRL sym range " + std::to_string((int)ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_symStart) + " to " + std::to_string((int)ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_numSym+(int)ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_symStart-1) + " of " + std::to_string(m_phyMacConfig->GetSymbolsPerSubframe()) + " layerIdx " + std::to_string((int)ret.m_sfAllocInfo.m_slotAllocInfo[0].m_dci.m_layerInd) + " of " + std::to_string((int)ret.m_sfAllocInfo.m_numAllocLayers));

  //pass the temporary SlotAllocInfo 2d lists to a single deque as expected by the PHY
  uint32_t finalSlotIdx = 1; //ctrl already inserted
  std::vector< std::deque <SlotAllocInfo>::iterator > itSlotDl, itSlotUl; //we insert all UL slots after all DL slots
  
  // Initialize numAllocLayers to the configured number of layers
  ret.m_sfAllocInfo.m_numAllocLayers = m_phyMacConfig->GetNumEnbLayers();
  
  for (uint8_t lay = 0; lay < m_phyMacConfig->GetNumEnbLayers (); lay++)
    {
      itSlotDl.push_back (tempDlslotAllocInfo[lay].begin ());
      itSlotUl.push_back (tempUlslotAllocInfo[lay].begin ());
    }


  bool done = false;
  while (!done)
    {
      layerIdx = 0;
      uint32_t val = m_phyMacConfig->GetSymbolsPerSubframe ();
      done = true; // if we do not find anything, skip
      for (uint8_t lay = 0; lay < ret.m_sfAllocInfo.m_numAllocLayers; lay++)
	{
          if (itSlotDl[lay] != tempDlslotAllocInfo[lay].end ())
            {
              if ( itSlotDl[lay]->m_dci.m_symStart < val )
        	{
                  layerIdx = lay;
                  val = itSlotDl[lay]->m_dci.m_symStart;
                  done = false;
                }
            }
        }
      if (done)
	{
          break;
        }
      itSlotDl[layerIdx]->m_slotIdx = finalSlotIdx++;
      ret.m_sfAllocInfo.m_slotAllocInfo.push_back (*(itSlotDl[layerIdx]));
      NS_LOG_INFO ("Fr "<< (int)ret.m_sfnSf.m_frameNum<<" Sf "<<(int)ret.m_sfnSf.m_sfNum <<" DL slot no. "<< finalSlotIdx -1 << " to UE "<< itSlotDl[layerIdx]->m_dci.m_rnti <<" sym range "<<(int) itSlotDl[layerIdx]->m_dci.m_symStart << " to "<<(int) itSlotDl[layerIdx]->m_dci.m_numSym+itSlotDl[layerIdx]->m_dci.m_symStart-1 << " of " << m_phyMacConfig->GetSymbolsPerSubframe () <<" layerIdx " << (int) itSlotDl[layerIdx]->m_dci.m_layerInd <<" of "<< (int) ret.m_sfAllocInfo.m_numAllocLayers);
      WriteLogToFile("Fr " + std::to_string((int)ret.m_sfnSf.m_frameNum) + " Sf " + std::to_string((int)ret.m_sfnSf.m_sfNum) + " DL slot no. " + std::to_string(finalSlotIdx -1) + " to UE " + std::to_string(itSlotDl[layerIdx]->m_dci.m_rnti) + " sym range " + std::to_string((int)itSlotDl[layerIdx]->m_dci.m_symStart) + " to " + std::to_string((int)itSlotDl[layerIdx]->m_dci.m_numSym+itSlotDl[layerIdx]->m_dci.m_symStart-1) + " of " + std::to_string(m_phyMacConfig->GetSymbolsPerSubframe()) + " layerIdx " + std::to_string((int)itSlotDl[layerIdx]->m_dci.m_layerInd) + " of " + std::to_string((int)ret.m_sfAllocInfo.m_numAllocLayers));
      itSlotDl[layerIdx]++;

      if (itSlotDl[layerIdx] == tempDlslotAllocInfo[layerIdx].end ())
	{
          done = true;
          for (uint8_t lay = 0; lay < m_phyMacConfig->GetNumEnbLayers (); lay++)
            {
              if ( itSlotDl[lay] != tempDlslotAllocInfo[lay].end () )
        	{
                  done = false;
                  break;
                }
            }
        }
    }

  done = false;
  while (!done)
    {
      layerIdx = 0;
      uint32_t val = m_phyMacConfig->GetSymbolsPerSubframe ();
      done = true; // if we do not find anything, skip
      for (uint8_t lay = 0; lay < m_phyMacConfig->GetNumEnbLayers (); lay++)
	{
          if (itSlotUl[lay] != tempUlslotAllocInfo[lay].end ())
            {
              if ( itSlotUl[lay]->m_dci.m_symStart < val )
        	{
                  layerIdx = lay;
                  val = itSlotUl[lay]->m_dci.m_symStart;
                  done = false;
                }
            }
        }
      if (done)
	{
          break;
        }
      itSlotUl[layerIdx]->m_slotIdx = finalSlotIdx++;
      ret.m_sfAllocInfo.m_slotAllocInfo.push_back (*(itSlotUl[layerIdx]));
      NS_LOG_INFO ("Fr "<< (int)ret.m_sfnSf.m_frameNum<<" Sf "<<(int)ret.m_sfnSf.m_sfNum <<" UL slot no. "<< finalSlotIdx -1 << " to UE "<< itSlotUl[layerIdx]->m_dci.m_rnti <<" sym range "<<(int) itSlotUl[layerIdx]->m_dci.m_symStart << " to "<<(int) itSlotUl[layerIdx]->m_dci.m_numSym+itSlotUl[layerIdx]->m_dci.m_symStart-1 << " of " << m_phyMacConfig->GetSymbolsPerSubframe () <<" layerIdx " << (int) itSlotUl[layerIdx]->m_dci.m_layerInd <<" of "<< (int) ret.m_sfAllocInfo.m_numAllocLayers);
      WriteLogToFile("Fr " + std::to_string((int)ret.m_sfnSf.m_frameNum) + " Sf " + std::to_string((int)ret.m_sfnSf.m_sfNum) + " UL slot no. " + std::to_string(finalSlotIdx -1) + " to UE " + std::to_string(itSlotUl[layerIdx]->m_dci.m_rnti) + " sym range " + std::to_string((int)itSlotUl[layerIdx]->m_dci.m_symStart) + " to " + std::to_string((int)itSlotUl[layerIdx]->m_dci.m_numSym+itSlotUl[layerIdx]->m_dci.m_symStart-1) + " of " + std::to_string(m_phyMacConfig->GetSymbolsPerSubframe()) + " layerIdx " + std::to_string((int)itSlotUl[layerIdx]->m_dci.m_layerInd) + " of " + std::to_string((int)ret.m_sfAllocInfo.m_numAllocLayers));
      itSlotUl[layerIdx]++;


      if (itSlotUl[layerIdx] == tempUlslotAllocInfo[layerIdx].end ())
	{
          done = true;
          for (uint8_t lay = 0; lay < m_phyMacConfig->GetNumEnbLayers (); lay++)
            {
              if ( itSlotUl[lay] != tempUlslotAllocInfo[lay].end () )
        	{
                  done = false;
                  break;
                }
            }
        }
    }

  //		do
  //		{
  //			std::cout << '\n' << "Press a key to continue...";
  //		} while (std::cin.get() != '\n');

  // add slot for UL control
  SlotAllocInfo ulCtrlSlot (0xFF, SlotAllocInfo::UL_slotAllocInfo, SlotAllocInfo::CTRL, SlotAllocInfo::DIGITAL, 0, 0);
  ulCtrlSlot.m_dci.m_numSym = 1;
  ulCtrlSlot.m_dci.m_symStart = m_phyMacConfig->GetSymbolsPerSubframe () - 1;
  ulCtrlSlot.m_dci.m_layerInd = 0;
  tempUlslotAllocInfo[layerIdx].push_back (ulCtrlSlot);
  ret.m_sfAllocInfo.m_slotAllocInfo.push_back (ulCtrlSlot);

  //	finalSlotIdx=ret.m_sfAllocInfo.m_slotAllocInfo.size()-1;
  NS_LOG_INFO ("Fr "<< (int)ret.m_sfnSf.m_frameNum<<" Sf "<<(int)ret.m_sfnSf.m_sfNum <<" UL slot no. "<< finalSlotIdx << " UL CTRL sym range "<<(int) ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_symStart << " to "<<(int) ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_numSym+(int) ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_symStart-1 << " of " << m_phyMacConfig->GetSymbolsPerSubframe () <<" layerIdx " << (int) ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_layerInd <<" of "<< (int) ret.m_sfAllocInfo.m_numAllocLayers);
  WriteLogToFile("Fr " + std::to_string((int)ret.m_sfnSf.m_frameNum) + " Sf " + std::to_string((int)ret.m_sfnSf.m_sfNum) + " UL slot no. " + std::to_string(finalSlotIdx) + " UL CTRL sym range " + std::to_string((int)ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_symStart) + " to " + std::to_string((int)ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_numSym+(int)ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_symStart-1) + " of " + std::to_string(m_phyMacConfig->GetSymbolsPerSubframe()) + " layerIdx " + std::to_string((int)ret.m_sfAllocInfo.m_slotAllocInfo[finalSlotIdx].m_dci.m_layerInd) + " of " + std::to_string((int)ret.m_sfAllocInfo.m_numAllocLayers));
  //  for (std::deque <SlotAllocInfo>::iterator itSlot = tempUlslotAllocInfo.begin (); itSlot != tempUlslotAllocInfo.end() ; itSlot++){
  //	  itSlot->m_slotIdx=tempDlslotIdx++;
  //	  ret.m_sfAllocInfo.m_slotAllocInfo.push_back(*itSlot);
  //  }

  m_macSchedSapUser->SchedConfigInd (ret);

  // Log scheduler footer
  std::stringstream footerMsg;
  footerMsg << "=========== SCHEDULER COMPLETE ===========";
  footerMsg << " Frame: " << (int)ret.m_sfnSf.m_frameNum;
  footerMsg << " Subframe: " << (int)ret.m_sfnSf.m_sfNum;
  footerMsg << " Total Slots: " << ret.m_sfAllocInfo.m_slotAllocInfo.size();
  footerMsg << " Total Symbols Allocated: " << ret.m_sfAllocInfo.m_numSymAlloc;
  footerMsg << " Total Layers: " << (int)ret.m_sfAllocInfo.m_numAllocLayers;
  footerMsg << " ========================================";
  WriteLogToFile(footerMsg.str());
  
  WriteLogToFile("DoSchedTriggerReq completed");

  return;
}

void
MmWavePaddedHbfMacScheduler::DoSchedUlMacCtrlInfoReq (const struct MmWaveMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters& params)
{
  NS_LOG_FUNCTION (this);

  std::map <uint16_t,uint32_t>::iterator it;

  for (unsigned int i = 0; i < params.m_macCeList.size (); i++)
    {
      if ( params.m_macCeList.at (i).m_macCeType == MacCeElement::BSR )
        {
          // display all contents of the buffer
          NS_LOG_DEBUG("BSR params: " << params.m_macCeList.at(i).m_rnti << " " << params.m_macCeList.at(i).m_macCeType << " " << params.m_macCeList.at(i).m_macCeValue.m_bufferStatus.at(0) << " " << params.m_macCeList.at(i).m_macCeValue.m_bufferStatus.at(1) << " " << params.m_macCeList.at(i).m_macCeValue.m_bufferStatus.at(2) << " " << params.m_macCeList.at(i).m_macCeValue.m_bufferStatus.at(3));

          // buffer status report
          // note that this scheduler does not differentiate the
          // allocation according to which LCGs have more/less bytes
          // to send.
          // Hence the BSR of different LCGs are just summed up to get
          // a total queue size that is used for allocation purposes.

          uint32_t buffer = 0;
          for (uint8_t lcg = 0; lcg < 4; ++lcg)
            {
              uint8_t bsrId = params.m_macCeList.at (i).m_macCeValue.m_bufferStatus.at (lcg);
              buffer += BsrId2BufferSize (bsrId);
            }

          uint16_t rnti = params.m_macCeList.at (i).m_rnti;
          it = m_ceBsrRxed.find (rnti);
          if (it == m_ceBsrRxed.end ())
            {
              // create the new entry
              m_ceBsrRxed.insert ( std::pair<uint16_t, uint32_t > (rnti, buffer));
              NS_LOG_INFO (this << " Insert RNTI " << rnti << " queue " << buffer);
            }
          else
            {
              // update the buffer size value
              (*it).second = buffer;
              NS_LOG_INFO (this << " Update RNTI " << rnti << " queue " << buffer);
            }
        }
    }

  return;
}

void
MmWavePaddedHbfMacScheduler::DoSchedSetMcs (int mcs)
{
  if (mcs >= 0 && mcs <= 28)
    {
      m_mcsDefaultDl = mcs;
      m_mcsDefaultUl = mcs;
    }
}

bool
MmWavePaddedHbfMacScheduler::SortRlcBufferReq (MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters i, MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters j)
{
  return (i.m_rnti < j.m_rnti);
}


void
MmWavePaddedHbfMacScheduler::RefreshDlCqiMaps (void)
{
  NS_LOG_FUNCTION (this << m_wbCqiTimers.size ());
  // refresh DL CQI P01 Map
  std::map <uint16_t,uint32_t>::iterator itP10 = m_wbCqiTimers.begin ();
  while (itP10 != m_wbCqiTimers.end ())
    {
      NS_LOG_INFO (this << " P10-CQI for user " << (*itP10).first << " is " << (uint32_t)(*itP10).second << " thr " << (uint32_t)m_cqiTimersThreshold);
      if ((*itP10).second == 0)
        {
          // delete correspondent entries
          std::map <uint16_t,uint8_t>::iterator itMap = m_wbCqiRxed.find ((*itP10).first);
          NS_ASSERT_MSG (itMap != m_wbCqiRxed.end (), " Does not find CQI report for user " << (*itP10).first);
          NS_LOG_INFO (this << " P10-CQI exired for user " << (*itP10).first);
          m_wbCqiRxed.erase (itMap);
          std::map <uint16_t,uint32_t>::iterator temp = itP10;
          itP10++;
          m_wbCqiTimers.erase (temp);
        }
      else
        {
          (*itP10).second--;
          itP10++;
        }
    }

  return;
}


void
MmWavePaddedHbfMacScheduler::RefreshUlCqiMaps (void)
{
  // refresh UL CQI  Map
  std::map <uint16_t,uint32_t>::iterator itUl = m_ueCqiTimers.begin ();
  while (itUl != m_ueCqiTimers.end ())
    {
      NS_LOG_INFO (this << " UL-CQI for user " << (*itUl).first << " is " << (uint32_t)(*itUl).second << " thr " << (uint32_t)m_cqiTimersThreshold);
      if ((*itUl).second == 0)
        {
          // delete correspondent entries
          std::map <uint16_t, struct UlCqiMapElem>::iterator itMap = m_ueUlCqi.find ((*itUl).first);
          NS_ASSERT_MSG (itMap != m_ueUlCqi.end (), " Does not find CQI report for user " << (*itUl).first);
          NS_LOG_INFO (this << " UL-CQI expired for user " << (*itUl).first);
          itMap->second.m_ueUlCqi.clear ();
          m_ueUlCqi.erase (itMap);
          std::map <uint16_t,uint32_t>::iterator temp = itUl;
          itUl++;
          m_ueCqiTimers.erase (temp);
        }
      else
        {
          (*itUl).second--;
          itUl++;
        }
    }

  return;
}

void
MmWavePaddedHbfMacScheduler::UpdateDlRlcBufferInfo (uint16_t rnti, uint8_t lcid, uint16_t size)
{
  NS_LOG_FUNCTION (this);
  std::list<MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it;
  for (it = m_rlcBufferReq.begin (); it != m_rlcBufferReq.end (); it++)
    {
      if (((*it).m_rnti == rnti) && ((*it).m_logicalChannelIdentity == lcid))
        {
          NS_LOG_INFO (this << " UE " << rnti << " LC " << (uint16_t)lcid << " txqueue " << (*it).m_rlcTransmissionQueueSize << " retxqueue " << (*it).m_rlcRetransmissionQueueSize << " status " << (*it).m_rlcStatusPduSize << " decrease " << size);
          // Update queues: RLC tx order Status, ReTx, Tx
          // Update status queue
          if (((*it).m_rlcStatusPduSize > 0) && (size >= (*it).m_rlcStatusPduSize))
            {
              (*it).m_rlcStatusPduSize = 0;
            }

          if ((*it).m_rlcRetransmissionQueueSize > 0)
            {
              if ((*it).m_rlcRetransmissionQueueSize <= (unsigned)(size - (*it).m_rlcStatusPduSize))
                {
                  (*it).m_rlcRetransmissionQueueSize = 0;
                }
              else
                {
                  (*it).m_rlcRetransmissionQueueSize -= (size - (*it).m_rlcStatusPduSize);
                }
            }
          else if ((*it).m_rlcTransmissionQueueSize > 0)
            {
              uint32_t rlcOverhead;
              if (lcid == 1)
                {
                  // for SRB1 (using RLC AM) it's better to
                  // overestimate RLC overhead rather than
                  // underestimate it and risk unneeded
                  // segmentation which increases delay
                  rlcOverhead = 4;
                }
              else
                {
                  // minimum RLC overhead due to header
                  rlcOverhead = 2;
                }
              // update transmission queue
              if ((*it).m_rlcTransmissionQueueSize <= (size - rlcOverhead - (*it).m_rlcStatusPduSize))
                {
                  (*it).m_rlcTransmissionQueueSize = 0;
                }
              else
                {
                  (*it).m_rlcTransmissionQueueSize -= (size - rlcOverhead - (*it).m_rlcStatusPduSize);
                }
            }
          return;
        }
    }
}

void
MmWavePaddedHbfMacScheduler::UpdateUlRlcBufferInfo (uint16_t rnti, uint16_t size)
{

  size = size - 2; // remove the minimum RLC overhead
  std::map <uint16_t,uint32_t>::iterator it = m_ceBsrRxed.find (rnti);
  if (it != m_ceBsrRxed.end ())
    {
      NS_LOG_INFO (this << " Update RLC BSR UE " << rnti << " size " << size << " BSR " << (*it).second);
      if ((*it).second >= size)
        {
          (*it).second -= size;
        }
      else
        {
          (*it).second = 0;
        }
    }
  else
    {
      NS_LOG_ERROR (this << " Does not find BSR report info of UE " << rnti);
    }

}


void
MmWavePaddedHbfMacScheduler::DoCschedCellConfigReq (const struct MmWaveMacCschedSapProvider::CschedCellConfigReqParameters& params)
{
  NS_LOG_FUNCTION (this);
  // Read the subset of parameters used
  m_cschedCellConfig = params;
  //m_rachAllocationMap.resize (m_cschedCellConfig.m_ulBandwidth, 0);
  MmWaveMacCschedSapUser::CschedUeConfigCnfParameters cnf;
  cnf.m_result = SUCCESS;
  m_macCschedSapUser->CschedUeConfigCnf (cnf);
  return;
}

void
MmWavePaddedHbfMacScheduler::DoCschedUeConfigReq (const struct MmWaveMacCschedSapProvider::CschedUeConfigReqParameters& params)
{
  NS_LOG_FUNCTION (this << " RNTI " << params.m_rnti << " txMode " << (uint16_t)params.m_transmissionMode);
  NS_LOG_DEBUG("*** UE ADDED TO SCHEDULER: RNTI=" << params.m_rnti << " IAB=" << params.m_ueCapabilities.m_iab << " numIabDevs=" << params.m_ueCapabilities.m_numIabDevsPerRnti << " at time " << Simulator::Now().GetSeconds() << "s");

  if (m_dlHarqProcessesStatus.find (params.m_rnti) == m_dlHarqProcessesStatus.end ())
    {
      //m_dlHarqCurrentProcessId.insert (std::pair <uint16_t,uint8_t > (params.m_rnti, 0));
      DlHarqProcessesStatus_t dlHarqPrcStatus;
      dlHarqPrcStatus.resize (m_phyMacConfig->GetNumHarqProcess (), 0);
      m_dlHarqProcessesStatus.insert (std::pair <uint16_t, DlHarqProcessesStatus_t> (params.m_rnti, dlHarqPrcStatus));
      DlHarqProcessesTimer_t dlHarqProcessesTimer;
      dlHarqProcessesTimer.resize (m_phyMacConfig->GetNumHarqProcess (),0);
      m_dlHarqProcessesTimer.insert (std::pair <uint16_t, DlHarqProcessesTimer_t> (params.m_rnti, dlHarqProcessesTimer));
      DlHarqProcessesDciInfoList_t dlHarqTbInfoList;
      dlHarqTbInfoList.resize (m_phyMacConfig->GetNumHarqProcess ());
      m_dlHarqProcessesDciInfoMap.insert (std::pair <uint16_t, DlHarqProcessesDciInfoList_t> (params.m_rnti, dlHarqTbInfoList));
      DlHarqRlcPduList_t dlHarqRlcPduList;
      dlHarqRlcPduList.resize (m_phyMacConfig->GetNumHarqProcess ());
      m_dlHarqProcessesRlcPduMap.insert (std::pair <uint16_t, DlHarqRlcPduList_t> (params.m_rnti, dlHarqRlcPduList));
    }

  if (m_ulHarqProcessesStatus.find (params.m_rnti) == m_ulHarqProcessesStatus.end ())
    {
      //				m_ulHarqCurrentProcessId.insert (std::pair <uint16_t,uint8_t > (rnti, 0));
      UlHarqProcessesStatus_t ulHarqPrcStatus;
      ulHarqPrcStatus.resize (m_phyMacConfig->GetNumHarqProcess (), 0);
      m_ulHarqProcessesStatus.insert (std::pair <uint16_t, UlHarqProcessesStatus_t> (params.m_rnti, ulHarqPrcStatus));
      UlHarqProcessesTimer_t ulHarqProcessesTimer;
      ulHarqProcessesTimer.resize (m_phyMacConfig->GetNumHarqProcess (),0);
      m_ulHarqProcessesTimer.insert (std::pair <uint16_t, UlHarqProcessesTimer_t> (params.m_rnti, ulHarqProcessesTimer));
      UlHarqProcessesDciInfoList_t ulHarqTbInfoList;
      ulHarqTbInfoList.resize (m_phyMacConfig->GetNumHarqProcess ());
      m_ulHarqProcessesDciInfoMap.insert (std::pair <uint16_t, UlHarqProcessesDciInfoList_t> (params.m_rnti, ulHarqTbInfoList));
    }

      // IAB configure if this rnti is an IAB dev or not!
  if(params.m_reconfigureFlag)
  {
  	uint32_t prevNumDevs = 0;
  	auto iterIab = m_rntiIabInfoMap.find(params.m_rnti);
  	if(iterIab != m_rntiIabInfoMap.end())
  	{
  		iterIab->second.first = params.m_ueCapabilities.m_iab;
  		prevNumDevs = iterIab->second.second;
  		iterIab->second.second = params.m_ueCapabilities.m_numIabDevsPerRnti;
  	}
  	else
  	{
  		auto pair = std::make_pair(params.m_rnti, std::make_pair(params.m_ueCapabilities.m_iab, params.m_ueCapabilities.m_numIabDevsPerRnti));
  		m_rntiIabInfoMap.insert(pair);
  	}
  	NS_LOG_DEBUG(this << " Reconfiguration of UE " << params.m_rnti << " iab " << params.m_ueCapabilities.m_iab
  		<< " numDevs " << params.m_ueCapabilities.m_numIabDevsPerRnti << " prevNumDevs " << prevNumDevs);

  	if(params.m_ueCapabilities.m_numIabDevsPerRnti + 1 > (int)m_maxSchedulingDelay) // + 1 since we need at least 1 TTI without no downstream IAB nodes
  	{
  		NS_LOG_WARN(this << " !!! --- !!! Updating m_maxSchedulingDelay from " 
  			<< m_maxSchedulingDelay << " to " << params.m_ueCapabilities.m_numIabDevsPerRnti + 1);
  		m_maxSchedulingDelay = params.m_ueCapabilities.m_numIabDevsPerRnti + 1;
  	}
  }
  NS_LOG_DEBUG("m_rntiIabInfoMap size " << m_rntiIabInfoMap.size());
}

void
MmWavePaddedHbfMacScheduler::DoCschedLcConfigReq (const struct MmWaveMacCschedSapProvider::CschedLcConfigReqParameters& params)
{
  NS_LOG_FUNCTION (this);
  // Not used at this stage (LCs updated by DoSchedDlRlcBufferReq)
  return;
}

void
MmWavePaddedHbfMacScheduler::DoCschedLcReleaseReq (const struct MmWaveMacCschedSapProvider::CschedLcReleaseReqParameters& params)
{
  NS_LOG_FUNCTION (this);
  for (uint16_t i = 0; i < params.m_logicalChannelIdentity.size (); i++)
    {
      std::list<MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it = m_rlcBufferReq.begin ();
      while (it != m_rlcBufferReq.end ())
        {
          if (((*it).m_rnti == params.m_rnti)&&((*it).m_logicalChannelIdentity == params.m_logicalChannelIdentity.at (i)))
            {
              it = m_rlcBufferReq.erase (it);
            }
          else
            {
              it++;
            }
        }
    }
  return;
}

void
MmWavePaddedHbfMacScheduler::DoCschedUeReleaseReq (const struct MmWaveMacCschedSapProvider::CschedUeReleaseReqParameters& params)
{
  NS_LOG_FUNCTION (this << " Release RNTI " << params.m_rnti);
  NS_LOG_DEBUG("*** UE REMOVED FROM SCHEDULER: RNTI=" << params.m_rnti << " at time " << Simulator::Now().GetSeconds() << "s");

  //m_dlHarqCurrentProcessId.erase (params.m_rnti);
  m_dlHarqProcessesStatus.erase  (params.m_rnti);
  m_dlHarqProcessesTimer.erase (params.m_rnti);
  m_dlHarqProcessesDciInfoMap.erase  (params.m_rnti);
  m_dlHarqProcessesRlcPduMap.erase  (params.m_rnti);
  //m_ulHarqCurrentProcessId.erase  (params.m_rnti);
  m_ulHarqProcessesTimer.erase (params.m_rnti);
  m_ulHarqProcessesStatus.erase  (params.m_rnti);
  m_ulHarqProcessesDciInfoMap.erase  (params.m_rnti);
  m_ceBsrRxed.erase (params.m_rnti);
  std::list<MmWaveMacSchedSapProvider::SchedDlRlcBufferReqParameters>::iterator it = m_rlcBufferReq.begin ();
  while (it != m_rlcBufferReq.end ())
    {
      if ((*it).m_rnti == params.m_rnti)
        {
          NS_LOG_INFO (this << " Erase RNTI " << (*it).m_rnti << " LC " << (uint16_t)(*it).m_logicalChannelIdentity);
          it = m_rlcBufferReq.erase (it);
        }
      else
        {
          it++;
        }
    }
  if (m_nextRntiUl == params.m_rnti)
    {
      m_nextRntiUl = 0;
    }

  if (m_nextRntiDl == params.m_rnti)
    {
      m_nextRntiDl = 0;
    }

  return;
}

// IAB methods
void
MmWavePaddedHbfMacScheduler::SetMmWaveUeMacCschedSapProvider(MmWaveUeMacCschedSapProvider* sap)
{
  m_iabBackahulSapProvider = sap;
}

MmWaveUeMacCschedSapProvider*
MmWavePaddedHbfMacScheduler::GetMmWaveUeMacCschedSapProvider()
{
  return m_iabBackahulSapProvider;
}

void
MmWavePaddedHbfMacScheduler::SetIabScheduler(bool iabScheduler)
{
  m_iabScheduler = iabScheduler;
}

std::string
MmWavePaddedHbfMacScheduler::PrintSubframeAllocationMask(std::vector<bool> mask)
{
  std::stringstream strStream;
  for(auto bit : mask)
    strStream << bit << " ";
  return strStream.str();
}

void
MmWavePaddedHbfMacScheduler::DoIabBackhaulSchedNotify(const struct MmWaveUeMacCschedSapProvider::IabBackhaulSchedInfo& info)
{
  NS_LOG_FUNCTION(this << info.m_dciInfoElementTdma.m_rnti);

  NS_ASSERT_MSG(m_iabScheduler, "Received DCI info for backhaul on a non IAB scheduler");

  NS_LOG_DEBUG("MmWavePaddedHbfMacScheduler received DCIs for the backhaul link, RNTI: " << info.m_dciInfoElementTdma.m_rnti 
                << " Frame: " << info.m_sfnSf.m_frameNum 
                << " Subframe: " << (uint16_t)info.m_sfnSf.m_sfNum 
                << " Symbols: " << (uint16_t)info.m_dciInfoElementTdma.m_symStart 
                << "-" << (uint16_t)(info.m_dciInfoElementTdma.m_symStart + info.m_dciInfoElementTdma.m_numSym - 1)
                << " Message ID: " << info.m_messageId
                << " at time " << Simulator::Now().GetSeconds() << "s");

  uint32_t subframe = info.m_sfnSf.m_sfNum;
  uint32_t frame = info.m_sfnSf.m_frameNum;

	// get the SfIabAllocInfo for this subframe
	SfIabAllocInfo currentInfo = m_iabBusySubframeAllocation.at(subframe);
	SfIabAllocInfo newInfo(m_phyMacConfig->GetSymbolsPerSubframe ());

  NS_LOG_DEBUG("currentInfo frame " << currentInfo.m_sfnSf.m_frameNum << " subframe " << (uint16_t)currentInfo.m_sfnSf.m_sfNum);

  if(currentInfo.m_sfnSf.m_frameNum == frame)
  {
    // another DCI has already been registered for this subframe
    newInfo = currentInfo;
    NS_LOG_DEBUG("This frame/subframe had already a DCI stored with mask " << PrintSubframeAllocationMask(newInfo.m_symAllocationMask));
  }
  else
  {
    newInfo.m_sfnSf = info.m_sfnSf;
  }

  uint32_t firstAllocatedIdx = info.m_dciInfoElementTdma.m_symStart;
  uint32_t nextFreeIdx = firstAllocatedIdx + info.m_dciInfoElementTdma.m_numSym;

  // check if it overlaps with already busy regions
  for(uint32_t index = firstAllocatedIdx; index < nextFreeIdx; index++)
  {
    if (newInfo.m_symAllocationMask.at(index) != 0) {
      NS_LOG_WARN("[SCHEDULER DOUBLE BOOKING] RNTI: " << info.m_dciInfoElementTdma.m_rnti
                  << ", frame: " << frame
                  << ", subframe: " << (uint16_t)info.m_sfnSf.m_sfNum
                  << ", symbol: " << index
                  << ", numSym: " << (uint16_t)info.m_dciInfoElementTdma.m_numSym
                  << ". Already allocated! Mask: " << PrintSubframeAllocationMask(newInfo.m_symAllocationMask));
    } else {
      NS_LOG_DEBUG("[SCHEDULER ALLOC] RNTI: " << info.m_dciInfoElementTdma.m_rnti
                   << ", frame: " << frame
                   << ", subframe: " << (uint16_t)info.m_sfnSf.m_sfNum
                   << ", symbol: " << index
                   << ", numSym: " << (uint16_t)info.m_dciInfoElementTdma.m_numSym);
    }
    NS_ASSERT_MSG(newInfo.m_symAllocationMask.at(index) == 0, "DCI signals that a symbol is scheduled for IAB, but it was already scheduled");
    newInfo.m_symAllocationMask.at(index) = 1;
  }
  NS_LOG_DEBUG("Mask " << PrintSubframeAllocationMask(newInfo.m_symAllocationMask));

  // DCI duplicate check
  static std::set<std::tuple<uint16_t, uint32_t, uint32_t, uint8_t, uint8_t>> sentDciSet;
  auto dciKey = std::make_tuple(info.m_dciInfoElementTdma.m_rnti, frame, subframe, info.m_dciInfoElementTdma.m_symStart, info.m_dciInfoElementTdma.m_numSym);
  if (sentDciSet.count(dciKey)) {
    NS_LOG_WARN("[SCHEDULER DUPLICATE DCI] RNTI: " << info.m_dciInfoElementTdma.m_rnti
                << ", frame: " << frame
                << ", subframe: " << subframe
                << ", symStart: " << (uint16_t)info.m_dciInfoElementTdma.m_symStart
                << ", numSym: " << (uint16_t)info.m_dciInfoElementTdma.m_numSym);
  } else {
    sentDciSet.insert(dciKey);
    NS_LOG_INFO("[SCHEDULER SEND DCI] RNTI: " << info.m_dciInfoElementTdma.m_rnti
                << ", frame: " << frame
                << ", subframe: " << subframe
                << ", symStart: " << (uint16_t)info.m_dciInfoElementTdma.m_symStart
                << ", numSym: " << (uint16_t)info.m_dciInfoElementTdma.m_numSym);
  }

  m_iabBusySubframeAllocation.at(subframe) = newInfo;
}

uint16_t
MmWavePaddedHbfMacScheduler::GetNumIabRnti()
{
  NS_LOG_FUNCTION(this);
  uint16_t numIabDevs = 0;
  // cycle through the list of BSRs, and check which RNTIs are for IAB devs
  for(auto itRlcBuf : m_rlcBufferReq)
  {
    uint16_t rnti = itRlcBuf.m_rnti;
    NS_LOG_INFO(this << " count rnti " << rnti);
    // get IAB info
    auto iabInfoIt = m_rntiIabInfoMap.find(rnti);
    if(iabInfoIt == m_rntiIabInfoMap.end() || !iabInfoIt->second.first)
    {
      // do nothing
    }
    else
    {
      if(((itRlcBuf.m_rlcTransmissionQueueSize > 0)
          || (itRlcBuf.m_rlcRetransmissionQueueSize > 0)
          || (itRlcBuf.m_rlcStatusPduSize > 0)))
      {
        numIabDevs++;
      }
    }
    NS_LOG_INFO(this << " numIabDevs " << numIabDevs);
  }

  return numIabDevs;
}

int
MmWavePaddedHbfMacScheduler::UpdateBusySymbolsForIab(uint8_t sfNum, uint8_t symIdx, int symAvail)
{
  NS_LOG_FUNCTION(this);
  // get the resources which are already set as busy for this subframe
  if(m_iabScheduler && !m_split)
  {
    SfIabAllocInfo busyResources = m_iabBusySubframeAllocation.at(sfNum);
    NS_LOG_DEBUG("Before check for IAB resources: symIdx " << (uint16_t)symIdx << " symAvail " << symAvail);
    if(busyResources.m_valid)
    {
      busyResources.m_valid = false; // signal that this SfIabAllocInfo has been used for a prev slot
      
      NS_LOG_DEBUG("This subframe has a DCI stored, with " <<
      " frame " << busyResources.m_sfnSf.m_frameNum << 
      " subframe " << (uint16_t)busyResources.m_sfnSf.m_sfNum <<
      PrintSubframeAllocationMask(busyResources.m_symAllocationMask));
    }
    m_iabBusySubframeAllocation.at(sfNum) = busyResources;

    NS_LOG_DEBUG("After check for IAB resources: symIdx " << (uint16_t)symIdx << " symAvail " << symAvail);
  }
  else if(m_iabScheduler && m_split)
  {
    NS_LOG_LOGIC("Before check for IAB resources: symIdx " << (uint16_t)symIdx << " symAvail " << symAvail);
    SfIabAllocInfo busyResources = m_iabBusySubframeAllocation.at(sfNum);
    if(busyResources.m_valid)
    {
      m_busyResourcesSchedSubframe = busyResources;

      // count the number of symbols which are busy
      int numBusySymbols = 0;
      for(auto symbolIter : m_busyResourcesSchedSubframe.m_symAllocationMask)
      {
        numBusySymbols += symbolIter;
      }

      symAvail -= numBusySymbols;

      NS_LOG_DEBUG("This subframe has a DCI stored, with " <<
      " frame " << busyResources.m_sfnSf.m_frameNum << 
      " subframe " << (uint16_t)busyResources.m_sfnSf.m_sfNum << " " <<
      PrintSubframeAllocationMask(busyResources.m_symAllocationMask) << 
      " total number of busy symbols " << numBusySymbols <<
      " symAvail " << symAvail);

    }
    else
    {
      m_busyResourcesSchedSubframe.m_valid = false;
    }
    m_iabBusySubframeAllocation.at(sfNum).m_valid = false;
  }

  return symAvail;
}

int
MmWavePaddedHbfMacScheduler::GetNumFreeSymbols(uint8_t symIdx, int numSymNeeded)
{
  NS_LOG_LOGIC("symIdx " << (uint16_t)symIdx << " numSymNeeded " << numSymNeeded);

  if(!m_iabScheduler || !m_busyResourcesSchedSubframe.m_valid)
  {
    return numSymNeeded;
  }

  int numFreeSymbols = 0;
  // get the possible overlapping region
  uint32_t loopEnd = std::min(symIdx + numSymNeeded, (int)m_busyResourcesSchedSubframe.m_symAllocationMask.size());
  NS_LOG_LOGIC("loopEnd " << loopEnd);
  for(uint32_t index = symIdx; index < loopEnd; ++index)
  {
    if(m_busyResourcesSchedSubframe.m_symAllocationMask.at(index) == 0)
    {
      numFreeSymbols++;
    }
    else
    {
      NS_LOG_LOGIC("Symbol busy, allocated " << numFreeSymbols << " out of " << numSymNeeded);
      break;
    }
  }
  return numFreeSymbols;
}

void
MmWavePaddedHbfMacScheduler::UpdateResourceMask(uint8_t start, int numSymbols)
{
  if(!m_iabScheduler)
  {
    NS_FATAL_ERROR("Try to update the mask for a non IAB scheduler");
  }

  for(uint32_t index = start; index < (uint32_t)(start + numSymbols); ++index)
  {
    m_busyResourcesSchedSubframe.m_symAllocationMask.at(index) = 1;
  }
    
}

uint8_t
MmWavePaddedHbfMacScheduler::GetFirstFreeSymbol(uint8_t symIdx, int numFreeSymbols)
{
  NS_LOG_LOGIC("symIdx " << (uint16_t)symIdx << " numFreeSymbols " << numFreeSymbols);

  if(!m_iabScheduler || !m_busyResourcesSchedSubframe.m_valid)
  {
    return symIdx + numFreeSymbols;
  }

  int index;
  for(index = symIdx + numFreeSymbols; index < (int)m_busyResourcesSchedSubframe.m_symAllocationMask.size(); ++index)
  {
    if(m_busyResourcesSchedSubframe.m_symAllocationMask.at(index) == 0)
    {
      NS_LOG_LOGIC("Free symbol found at " << index);
      break;
    }
  }
  return static_cast<uint8_t>(index);
}

bool
MmWavePaddedHbfMacScheduler::CheckOverlapWithBusyResources(uint32_t symStart, uint32_t numSym, uint8_t layer)
{
  NS_LOG_LOGIC("symStart " << symStart << " numSym " << numSym << " layer " << (uint16_t)layer);

  if(!m_iabScheduler || !m_busyResourcesSchedSubframe.m_valid)
  {
  return false;
}

  uint32_t loopEnd = std::min(symStart + numSym, (uint32_t)m_busyResourcesSchedSubframe.m_symAllocationMask.size());
  NS_LOG_LOGIC("loopEnd " << loopEnd);
  for(uint32_t index = symStart; index < loopEnd; ++index)
  {
    if(m_busyResourcesSchedSubframe.m_symAllocationMask.at(index) == 1)
    {
      NS_LOG_LOGIC("overlapping from symbol " << index);
      return true;
    }
  }
  return false;
}

void
MmWavePaddedHbfMacScheduler::WriteLogToFile(const std::string& message)
{
  std::ofstream logFile;
  logFile.open("scheduler_logs.txt", std::ios::app);
  if (logFile.is_open()) {
    logFile << "Time: " << Simulator::Now().GetSeconds() << "s - this: " << this << " - " << message << std::endl;
    logFile.close();
  }
}

} // namespace ns3
