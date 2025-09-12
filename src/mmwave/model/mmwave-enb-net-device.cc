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
 *        	 	  Sourjya Dutta <sdutta@nyu.edu>
 *        	 	  Russell Ford <russell.ford@nyu.edu>
 *        		  Menglei Zhang <menglei@nyu.edu>
 *
 * Modified by: Muhammad Adeel Zahid <zahidma@myumanitoba.ca>
 *                 Integrating NTNs & Multilayer support with IAB derived from ns3-mmwave-iab, ns3-ntn and ns3-mmwave-hbf
 *                 
 */


#include <ns3/llc-snap-header.h>
#include <ns3/simulator.h>
#include <ns3/callback.h>
#include <ns3/node.h>
#include <ns3/packet.h>
#include "mmwave-net-device.h"
#include <ns3/packet-burst.h>
#include <ns3/uinteger.h>
#include <ns3/trace-source-accessor.h>
#include <ns3/pointer.h>
#include <ns3/enum.h>
#include <ns3/uinteger.h>
#include "mmwave-enb-net-device.h"
#include "mmwave-ue-net-device.h"
#include <ns3/lte-enb-rrc.h>
#include <ns3/ipv4-l3-protocol.h>
#include <ns3/abort.h>
#include <ns3/log.h>

namespace ns3{

NS_LOG_COMPONENT_DEFINE ("MmWaveEnbNetDevice");

NS_OBJECT_ENSURE_REGISTERED ( MmWaveEnbNetDevice);

TypeId MmWaveEnbNetDevice::GetTypeId()
{
	static TypeId
	    tid =
	    TypeId ("ns3::MmWaveEnbNetDevice")
	    .SetParent<MmWaveNetDevice> ()
	    .AddConstructor<MmWaveEnbNetDevice> ()
		.AddAttribute ("MmWaveEnbPhy",
			           "The PHY associated to this EnbNetDevice",
			           PointerValue (),
			           MakePointerAccessor (&MmWaveEnbNetDevice::m_phy),
		               MakePointerChecker <MmWaveEnbPhy> ())
		.AddAttribute ("MmWaveEnbMac",
					   "The MAC associated to this EnbNetDevice",
					   PointerValue (),
					   MakePointerAccessor (&MmWaveEnbNetDevice::m_mac),
					   MakePointerChecker <MmWaveEnbMac> ())
		.AddAttribute ("mmWaveScheduler",
						"The Scheduler associated with the MAC",
						PointerValue (),
					    MakePointerAccessor (&MmWaveEnbNetDevice::m_scheduler),
					    MakePointerChecker <MmWaveMacScheduler> ())
		.AddAttribute ("LteEnbRrc",
						"The RRC layer associated with the ENB",
						PointerValue (),
						MakePointerAccessor (&MmWaveEnbNetDevice::m_rrc),
						MakePointerChecker <LteEnbRrc> ())
		.AddAttribute ("CellId",
					   "Cell Identifier",
					   UintegerValue (0),
					   MakeUintegerAccessor (&MmWaveEnbNetDevice::m_cellId),
					   MakeUintegerChecker<uint16_t> ())
		.AddAttribute ("AntennaNum",
					   "Antenna number of the device",
					   UintegerValue (64),
					   MakeUintegerAccessor (&MmWaveEnbNetDevice::SetAntennaNum,
											 &MmWaveEnbNetDevice::GetAntennaNum),
					   MakeUintegerChecker<uint8_t> ())
	;

	return tid;
}

MmWaveEnbNetDevice::MmWaveEnbNetDevice()
	:m_cellId(0),
	 m_Bandwidth (72),
	 m_Earfcn(1),
	 m_isConstructed (false),
	 m_isConfigured (false),
	 m_numLayers(1)  // Default to single layer
{
	NS_LOG_FUNCTION (this);
}

MmWaveEnbNetDevice::~MmWaveEnbNetDevice()
{
	NS_LOG_FUNCTION (this);
}

void
MmWaveEnbNetDevice::DoInitialize(void)
{
	NS_LOG_FUNCTION(this);
	m_isConstructed = true;
	UpdateConfig ();
	
	// Initialize all PHY layers
	if (m_phyLayers.empty() && m_phy != 0)
	{
		// If no layers are set up, use the legacy single PHY
		m_phyLayers.push_back(m_phy);
		m_numLayers = 1;
	}
	
	for (auto& phy : m_phyLayers)
	{
		if (phy != 0)
		{
			phy->Initialize ();
		}
	}
}

void
MmWaveEnbNetDevice::DoDispose()
{
	NS_LOG_FUNCTION (this);
	
	// Dispose all PHY layers
	for (auto& phy : m_phyLayers)
	{
		if (phy != 0)
		{
			phy->Dispose();
		}
	}
	m_phyLayers.clear();
	
	// Dispose all MAC layers
	for (auto& mac : m_macLayers)
	{
		if (mac != 0)
		{
			mac->Dispose();
		}
	}
	m_macLayers.clear();
}

Ptr<MmWaveEnbPhy>
MmWaveEnbNetDevice::GetPhy (void) const
{
	NS_LOG_FUNCTION (this);
	// Return the first PHY layer for backward compatibility
	if (!m_phyLayers.empty())
	{
		return m_phyLayers[0];
	}
	return m_phy;  // Fallback to legacy PHY
}

Ptr<MmWaveEnbPhy>
MmWaveEnbNetDevice::GetPhy (uint8_t layerIndex)
{
	NS_LOG_FUNCTION (this << (uint32_t)layerIndex);
	if (layerIndex < m_phyLayers.size())
	{
		return m_phyLayers[layerIndex];
	}
	NS_LOG_WARN("Layer index " << (uint32_t)layerIndex << " out of range, returning first layer");
	return GetPhy();  // Return first layer as fallback
}

uint8_t
MmWaveEnbNetDevice::GetNumLayers () const
{
	NS_LOG_FUNCTION (this);
	return m_numLayers;
}

void
MmWaveEnbNetDevice::SetNumLayers (uint8_t numLayers)
{
	NS_LOG_FUNCTION (this << (uint32_t)numLayers);
	m_numLayers = numLayers;
	
	// Resize vectors if needed
	if (m_phyLayers.size() < numLayers)
	{
		m_phyLayers.resize(numLayers);
	}
	if (m_macLayers.size() < numLayers)
	{
		m_macLayers.resize(numLayers);
	}
}

Ptr<MmWaveEnbMac>
MmWaveEnbNetDevice::GetMac (uint8_t layerIndex)
{
	NS_LOG_FUNCTION (this << (uint32_t)layerIndex);
	if (layerIndex < m_macLayers.size())
	{
		return m_macLayers[layerIndex];
	}
	NS_LOG_WARN("Layer index " << (uint32_t)layerIndex << " out of range, returning first layer");
	return GetMac();  // Return first layer as fallback
}

uint16_t
MmWaveEnbNetDevice::GetCellId () const
{
	NS_LOG_FUNCTION (this);
	return m_cellId;
}

uint8_t
MmWaveEnbNetDevice::GetBandwidth () const
{
	NS_LOG_FUNCTION (this);
	return m_Bandwidth;
}

void
MmWaveEnbNetDevice::SetBandwidth (uint8_t bw)
{
	NS_LOG_FUNCTION (this);
	m_Bandwidth = bw;
}

void
MmWaveEnbNetDevice::SetEarfcn(uint16_t earfcn)
{
	NS_LOG_FUNCTION (this);
	m_Earfcn = earfcn;
}

uint16_t
MmWaveEnbNetDevice::GetEarfcn() const
{
	NS_LOG_FUNCTION (this);
	return m_Earfcn;

}

void
MmWaveEnbNetDevice::SetMac (Ptr<MmWaveEnbMac> mac)
{
	m_mac = mac;
	// Also set as first layer for multi-beam support
	if (m_macLayers.empty())
	{
		m_macLayers.push_back(mac);
	}
	else
	{
		m_macLayers[0] = mac;
	}
}

Ptr<MmWaveEnbMac>
MmWaveEnbNetDevice::GetMac (void)
{
	// Return the first MAC layer for backward compatibility
	if (!m_macLayers.empty())
	{
		return m_macLayers[0];
	}
	return m_mac;  // Fallback to legacy MAC
}

void
MmWaveEnbNetDevice::SetRrc (Ptr<LteEnbRrc> rrc)
{
	m_rrc = rrc;
}

Ptr<LteEnbRrc>
MmWaveEnbNetDevice::GetRrc (void)
{
	return m_rrc;
}

void
MmWaveEnbNetDevice::SetAntennaNum (uint8_t antennaNum)
{
	m_antennaNum = antennaNum;
}

void
MmWaveEnbNetDevice::SetPhyLayer (uint8_t layerIndex, Ptr<MmWaveEnbPhy> phy)
{
	NS_LOG_FUNCTION (this << (uint32_t)layerIndex << phy);
	if (layerIndex >= m_phyLayers.size())
	{
		m_phyLayers.resize(layerIndex + 1);
	}
	m_phyLayers[layerIndex] = phy;
	
	// Update legacy PHY reference for backward compatibility
	if (layerIndex == 0)
	{
		m_phy = phy;
	}
}

void
MmWaveEnbNetDevice::SetMacLayer (uint8_t layerIndex, Ptr<MmWaveEnbMac> mac)
{
	NS_LOG_FUNCTION (this << (uint32_t)layerIndex << mac);
	if (layerIndex >= m_macLayers.size())
	{
		m_macLayers.resize(layerIndex + 1);
	}
	m_macLayers[layerIndex] = mac;
	
	// Update legacy MAC reference for backward compatibility
	if (layerIndex == 0)
	{
		m_mac = mac;
	}
}
uint8_t
MmWaveEnbNetDevice::GetAntennaNum () const
{
	return m_antennaNum;
}

bool
MmWaveEnbNetDevice::DoSend (Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
	NS_LOG_FUNCTION (this << packet   << dest << protocolNumber);
	NS_ASSERT_MSG (protocolNumber == Ipv4L3Protocol::PROT_NUMBER, "unsupported protocol " << protocolNumber << ", only IPv4 is supported");

	return m_rrc->SendData (packet);
}

void
MmWaveEnbNetDevice::UpdateConfig (void)
{
  NS_LOG_FUNCTION (this);

	if (m_isConstructed)
	{
		if (!m_isConfigured)
		{
			NS_LOG_LOGIC (this << " Configure cell " << m_cellId);
			// we have to make sure that this function is called only once
			m_rrc->ConfigureCell (m_Bandwidth, m_Bandwidth, m_Earfcn, m_Earfcn, m_cellId);
			m_isConfigured = true;
		}

		//m_rrc->SetCsgId (m_csgId, m_csgIndication);
	}
	else
	{
		/*
		* Lower layers are not ready yet, so do nothing now and expect
		* ``DoInitialize`` to re-invoke this function.
		*/
	}
}

}


