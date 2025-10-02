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
 *  
 *   Author: Marco Mezzavilla < mezzavilla@nyu.edu>
 *        	 Sourjya Dutta <sdutta@nyu.edu>
 *        	 Russell Ford <russell.ford@nyu.edu>
 *        	 Menglei Zhang <menglei@nyu.edu>
 *
 * Modified by: Muhammad Adeel Zahid <zahidma@myumanitoba.ca>
 *                 Integrating NTNs & Multilayer support with IAB derived from signetlabdei/ns3-mmwave-iab, Mattia Sandri/ns3-ntn and signetlabdei/ns3-mmwave-hbf
 *                 
 */



#include "mmwave-3gpp-propagation-loss-model.h"
#include <ns3/log.h>
#include "ns3/mobility-model.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include <ns3/simulator.h>
#include <ns3/mmwave-ue-net-device.h>
#include <ns3/mmwave-iab-net-device.h>
#include <ns3/mc-ue-net-device.h>
#include <ns3/node.h>
using namespace ns3;


// ------------------------------------------------------------------------- //
NS_LOG_COMPONENT_DEFINE ("MmWave3gppPropagationLossModel");

NS_OBJECT_ENSURE_REGISTERED (MmWave3gppPropagationLossModel);

/// NTN Suburban LOS probabilities from table 6.6.1-1 of 3GPP 38.811
const std::map<int, double> SuburbanRuralLOSProb{
    {10, {78.2}},
    {20, {86.9}},
    {30, {91.9}},
    {40, {92.9}},
    {50, {93.5}},
    {60, {94.0}},
    {70, {94.9}},
    {80, {95.2}},
    {90, {99.8}},
};
/**
 * The enumerator used for code clarity when performing parameter assignment in the GetLoss Methods
 */
enum SFCL_params
{
    S_LOS_sigF,
    S_NLOS_sigF,
    S_NLOS_CL,
    Ka_LOS_sigF,
    Ka_NLOS_sigF,
    Ka_NLOS_CL,
};

/**
 * The map containing the 3GPP value regarding Shadow Fading and Clutter Loss tables for the
 * NTN Dense Urban scenario
 */
const std::map<int, std::vector<float>> SFCL_DenseUrban{
    {10, {3.5, 15.5, 34.3, 2.9, 17.1, 44.3}},
    {20, {3.4, 13.9, 30.9, 2.4, 17.1, 39.9}},
    {30, {2.9, 12.4, 29.0, 2.7, 15.6, 37.5}},
    {40, {3.0, 11.7, 27.7, 2.4, 14.6, 35.8}},
    {50, {3.1, 10.6, 26.8, 2.4, 14.2, 34.6}},
    {60, {2.7, 10.5, 26.2, 2.7, 12.6, 33.8}},
    {70, {2.5, 10.1, 25.8, 2.6, 12.1, 33.3}},
    {80, {2.3, 9.2, 25.5, 2.8, 12.3, 33.0}},
    {90, {1.2, 9.2, 25.5, 0.6, 12.3, 32.9}},
};

/**
 * The map containing the 3GPP value regarding Shadow Fading and Clutter Loss tables for the
 * NTN Urban scenario
 */
const std::map<int, std::vector<float>> SFCL_Urban{
    {10, {4, 6, 34.3, 4, 6, 44.3}},
    {20, {4, 6, 30.9, 4, 6, 39.9}},
    {30, {4, 6, 29.0, 4, 6, 37.5}},
    {40, {4, 6, 27.7, 4, 6, 35.8}},
    {50, {4, 6, 26.8, 4, 6, 34.6}},
    {60, {4, 6, 26.2, 4, 6, 33.8}},
    {70, {4, 6, 25.8, 4, 6, 33.3}},
    {80, {4, 6, 25.5, 4, 6, 33.0}},
    {90, {4, 6, 25.5, 4, 6, 32.9}},
};

/**
 * The map containing the 3GPP value regarding Shadow Fading and Clutter Loss tables for the
 * NTN Suburban and Rural scenarios
 */
const std::map<int, std::vector<float>> SFCL_SuburbanRural{
    {10, {1.79, 8.93, 19.52, 1.9, 10.7, 29.5}},
    {20, {1.14, 9.08, 18.17, 1.6, 10.0, 24.6}},
    {30, {1.14, 8.78, 18.42, 1.9, 11.2, 21.9}},
    {40, {0.92, 10.25, 18.28, 2.3, 11.6, 20.0}},
    {50, {1.42, 10.56, 18.63, 2.7, 11.8, 18.7}},
    {60, {1.56, 10.74, 17.68, 3.1, 10.8, 17.8}},
    {70, {0.85, 10.17, 16.5, 3.0, 10.8, 17.2}},
    {80, {0.72, 11.52, 16.3, 3.6, 10.8, 16.9}},
    {90, {0.72, 11.52, 16.3, 0.4, 10.8, 16.8}},
};

/**
 * Array containing the attenuation given by atmospheric absorption. 100 samples are selected for
 * frequencies from 1GHz to 100GHz. In order to get the atmospheric absorption loss for a given
 * frequency f: 1- round f to the closest integer between 0 and 100. 2- use the obtained integer to
 * access the corresponding element in the array, that will give the attenuation at that frequency.
 * Data is obtained form ITU-R P.676 Figure 6.
 */
const double atmosphericAbsorption[101] = {
    0,        0.0300,   0.0350,  0.0380,  0.0390,  0.0410,  0.0420,  0.0450,  0.0480,   0.0500,
    0.0530,   0.0587,   0.0674,  0.0789,  0.0935,  0.1113,  0.1322,  0.1565,  0.1841,   0.2153,
    0.2500,   0.3362,   0.4581,  0.5200,  0.5200,  0.5000,  0.4500,  0.3850,  0.3200,   0.2700,
    0.2500,   0.2517,   0.2568,  0.2651,  0.2765,  0.2907,  0.3077,  0.3273,  0.3493,   0.3736,
    0.4000,   0.4375,   0.4966,  0.5795,  0.6881,  0.8247,  0.9912,  1.1900,  1.4229,   1.6922,
    2.0000,   4.2654,   10.1504, 19.2717, 31.2457, 45.6890, 62.2182, 80.4496, 100.0000, 140.0205,
    170.0000, 100.0000, 78.1682, 59.3955, 43.5434, 30.4733, 20.0465, 12.1244, 6.5683,   3.2397,
    2.0000,   1.7708,   1.5660,  1.3858,  1.2298,  1.0981,  0.9905,  0.9070,  0.8475,   0.8119,
    0.8000,   0.8000,   0.8000,  0.8000,  0.8000,  0.8000,  0.8000,  0.8000,  0.8000,   0.8000,
    0.8000,   0.8029,   0.8112,  0.8243,  0.8416,  0.8625,  0.8864,  0.9127,  0.9408,   0.9701,
    1.0000};

/**
 * Map containing the Tropospheric attenuation in dB with 99% probability at 20 GHz in Toulouse
 * used for tropospheric scintillation losses. From Table 6.6.6.2.1-1 of 3GPP TR 38.811.
 */
const std::map<int, float> troposphericScintillationLoss{
    {10, {1.08}},
    {20, {0.48}},
    {30, {0.30}},
    {40, {0.22}},
    {50, {0.17}},
    {60, {0.13}},
    {70, {0.12}},
    {80, {0.12}},
    {90, {0.12}},
};

/**
 * \brief The nested map containing the Shadow Fading and
 *        Clutter Loss values for the NTN Suburban and Rural scenario
 */
const std::map<int, std::vector<float>>* m_SFCL_SuburbanRural = &SFCL_SuburbanRural;

/**
 * \brief The nested map containing the Shadow Fading and
 *        Clutter Loss values for the NTN Urban scenario
 */
const std::map<int, std::vector<float>>* m_SFCL_Urban = &SFCL_Urban;

/**
 * \brief The nested map containing the Shadow Fading and
 *        Clutter Loss values for the NTN Dense Urban scenario
 */
const std::map<int, std::vector<float>>* m_SFCL_DenseUrban = &SFCL_DenseUrban;

TypeId
MmWave3gppPropagationLossModel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MmWave3gppPropagationLossModel")
    .SetParent<PropagationLossModel> ()
    .AddConstructor<MmWave3gppPropagationLossModel> ()
    .AddAttribute ("MinLoss",
                   "The minimum value (dB) of the total loss, used at short ranges.",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&MmWave3gppPropagationLossModel::SetMinLoss,
                                       &MmWave3gppPropagationLossModel::GetMinLoss),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("ChannelCondition",
					"'l' for LOS, 'n' for NLOS, 'a' for all",
					StringValue ("a"),
					MakeStringAccessor (&MmWave3gppPropagationLossModel::m_channelConditions),
					MakeStringChecker ())
	.AddAttribute ("Scenario",
				"The available channel scenarios are 'RMa', 'UMa', 'UMi-StreetCanyon', 'InH-OfficeMixed', 'InH-OfficeOpen', 'InH-ShoppingMall'",
				StringValue ("RMa"),
				MakeStringAccessor (&MmWave3gppPropagationLossModel::m_scenario),
				MakeStringChecker ())
	.AddAttribute ("OptionalNlos",
				"Use the optional NLoS propagation loss model",
				BooleanValue (false),
				MakeBooleanAccessor (&MmWave3gppPropagationLossModel::m_optionNlosEnabled),
				MakeBooleanChecker ())
	.AddAttribute ("Shadowing",
				"Enable shadowing effect",
				BooleanValue (true),
				MakeBooleanAccessor (&MmWave3gppPropagationLossModel::m_shadowingEnabled),
				MakeBooleanChecker ())
	.AddAttribute ("InCar",
				"If inside a vehicle, car penetration loss should be added to propagation loss",
				BooleanValue (false),
				MakeBooleanAccessor (&MmWave3gppPropagationLossModel::m_inCar),
				MakeBooleanChecker ())
	.AddAttribute ("BuildingPenetrationLossEnabled",
				"Is building Penetration Loss to be added to propagation loss for NTN",
				BooleanValue (false),
				MakeBooleanAccessor (&MmWave3gppPropagationLossModel::m_buildingPenLossesEnabled),
				MakeBooleanChecker ())							
	.AddAttribute("O2iThreshold",
				"Specifies what will be the ratio of O2I channel "
				"conditions. Default value is 0 that corresponds to 0 O2I losses.",
				DoubleValue(0.0),
				MakeDoubleAccessor(&MmWave3gppPropagationLossModel::m_o2iThreshold),
				MakeDoubleChecker<double>(0, 1))
	.AddAttribute("O2iLowLossThreshold",
				"Specifies what will be the ratio of O2I "
				"low - high penetration losses. Default value is 1.0 meaning that"
				"all losses will be low",
				DoubleValue(1.0),
				MakeDoubleAccessor(&MmWave3gppPropagationLossModel::m_o2iLowLossThreshold),
				MakeDoubleChecker<double>(0, 1))
	.AddAttribute("LinkO2iConditionToAntennaHeight",
				"Specifies whether the O2I condition will "
				"be determined based on the UE height, i.e. if the UE height is 1.5 then "
				"it is O2O, "
				"otherwise it is O2I.",
				BooleanValue(false),
				MakeBooleanAccessor(
					&MmWave3gppPropagationLossModel::m_linkO2iConditionToAntennaHeight),
				MakeBooleanChecker())
	.AddAttribute("NTNScenario",
				"Set the NTN scenario as defined by TR 38.811 eg. Urban Micro etc.",
				StringValue("UMa"),
				MakeStringAccessor(&MmWave3gppPropagationLossModel::m_ntnScenario),
				MakeStringChecker())
  ;
  return tid;
}

MmWave3gppPropagationLossModel::MmWave3gppPropagationLossModel ()
{
	m_channelConditionMap.clear ();
	m_norVar = CreateObject<NormalRandomVariable> ();
	m_norVar->SetAttribute ("Mean", DoubleValue (0));
	m_norVar->SetAttribute ("Variance", DoubleValue (1));

	m_uniformVar = CreateObject<UniformRandomVariable> ();
	m_uniformVar->SetAttribute ("Min", DoubleValue (0));
	m_uniformVar->SetAttribute ("Max", DoubleValue (1));
  
	m_normRandomVariable = CreateObject<NormalRandomVariable>();
	m_normRandomVariable->SetAttribute("Mean", DoubleValue(0));
	m_normRandomVariable->SetAttribute("Variance", DoubleValue(1));

    m_randomO2iVar1 = CreateObject<UniformRandomVariable>();
    m_randomO2iVar1->SetAttribute("Min", DoubleValue(0));
    m_randomO2iVar1->SetAttribute("Max", DoubleValue(1));

    m_randomO2iVar2 = CreateObject<UniformRandomVariable>();
    m_randomO2iVar2->SetAttribute("Min", DoubleValue(0));
    m_randomO2iVar2->SetAttribute("Max", DoubleValue(1));

    m_normalO2iLowLossVar = CreateObject<NormalRandomVariable>();
    m_normalO2iLowLossVar->SetAttribute("Mean", DoubleValue(0));
    m_normalO2iLowLossVar->SetAttribute("Variance", DoubleValue(1));

    m_normalO2iHighLossVar = CreateObject<NormalRandomVariable>();
    m_normalO2iHighLossVar->SetAttribute("Mean", DoubleValue(0));
    m_normalO2iHighLossVar->SetAttribute("Variance", DoubleValue(1));

    m_uniformO2iLowHighLossVar = CreateObject<UniformRandomVariable>();
    m_uniformO2iLowHighLossVar->SetAttribute("Min", DoubleValue(0));
    m_uniformO2iLowHighLossVar->SetAttribute("Max", DoubleValue(1));

    m_uniformVarO2i = CreateObject<UniformRandomVariable>();
    m_uniformVarO2i->SetAttribute("Min", DoubleValue(0));
    m_uniformVarO2i->SetAttribute("Max", DoubleValue(1));	
}

void
MmWave3gppPropagationLossModel::SetMinLoss (double minLoss)
{
  m_minLoss = minLoss;
}
double
MmWave3gppPropagationLossModel::GetMinLoss (void) const
{
  return m_minLoss;
}

double
MmWave3gppPropagationLossModel::GetFrequency (void) const
{
  return m_frequency;
}


double
MmWave3gppPropagationLossModel::DoCalcRxPower (double txPowerDbm,
                                          Ptr<MobilityModel> a,
                                          Ptr<MobilityModel> b) const
{
	  return (txPowerDbm - GetLoss (a, b));
}

std::tuple<Ptr<MobilityModel>, Ptr<MobilityModel>, bool >
MmWave3gppPropagationLossModel::GetEnbUePair(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
	bool skip = false;
	Ptr<MobilityModel> ueMob=0, enbMob=0;
	if(DynamicCast<MmWaveEnbNetDevice> (a->GetObject<Node> ()->GetDevice(0)) != 0)
	{
		// for sure it is downlink
		enbMob = a;
		ueMob = b;
		if(DynamicCast<MmWaveEnbNetDevice> (b->GetObject<Node> ()->GetDevice(0)) != 0)
		{
			skip = true;
		}
	}
	else if(DynamicCast<MmWaveEnbNetDevice> (b->GetObject<Node> ()->GetDevice(0)) != 0)
	{
		// for sure it is uplink
		ueMob = a;
		enbMob = b;
	}
	else
	{
		// uplink or IAB downlink?
		if(DynamicCast<MmWaveIabNetDevice> (a->GetObject<Node> ()->GetDevice(0)) != 0)
		{
			if(DynamicCast<MmWaveIabNetDevice> (b->GetObject<Node> ()->GetDevice(0)) == 0)
			{
				// IAB to MC UE or UE (access)
				ueMob = b;
				enbMob = a;
			}
			else
			{
				// IAB to IAB
				Ptr<MmWaveIabNetDevice> txDev = DynamicCast<MmWaveIabNetDevice> (a->GetObject<Node> ()->GetDevice(0));
				Ptr<MmWaveIabNetDevice> rxDev = DynamicCast<MmWaveIabNetDevice> (b->GetObject<Node> ()->GetDevice(0));
				if(rxDev->GetBackhaulTargetEnb()) // downlink BH
				{
					enbMob = a;
					ueMob = b;
				}
				else // uplink BH or interference
				{
					ueMob = a;
					enbMob = b;
				}
			}
		}
		else if(DynamicCast<MmWaveIabNetDevice> (b->GetObject<Node> ()->GetDevice(0)) != 0)
		{
			// UE and MC UE to IAB
			ueMob = a;
			enbMob = b;
		}
		else 
		{
			// UE to UE or MC UE to MC UE
			skip = true;
		}
	}

	return std::make_tuple(enbMob, ueMob, skip);
}

Ptr<MmWavePhy> GetMmWavePhy(Ptr<const MobilityModel> mobility) 
{

    Ptr<const Node> node = mobility->GetObject<Node>();
    if (node == nullptr) 
	{
        NS_LOG_UNCOND("Error: No Node found for MobilityModel");
        return nullptr;
    }

    Ptr<NetDevice> device = node->GetDevice(0);
    if (device == nullptr) 
	{
        NS_LOG_UNCOND("Error: No NetDevice found on Node");
        return nullptr;
    }

    Ptr<MmWaveEnbNetDevice> enbDevice = DynamicCast<MmWaveEnbNetDevice>(device);
    Ptr<MmWaveUeNetDevice> ueDevice = DynamicCast<MmWaveUeNetDevice>(device);
    Ptr<MmWaveIabNetDevice> iabDevice = DynamicCast<MmWaveIabNetDevice>(device);

    Ptr<MmWavePhy> phy = nullptr;

    if (enbDevice != nullptr) 
	{
        phy = enbDevice->GetPhy(); 
    } 
    else if (ueDevice != nullptr) 
	{
        phy = ueDevice->GetPhy();
    } 
    else if (iabDevice != nullptr) 
	{
        Ptr<MmWavePhy> accessPhy = iabDevice->GetAccessPhy();
        Ptr<MmWavePhy> backhaulPhy = iabDevice->GetBackhaulPhy();

        if (accessPhy != nullptr) 
		{
            return accessPhy;
        }
        if (backhaulPhy != nullptr) 
		{
            return backhaulPhy;
        }

        NS_FATAL_ERROR("IAB device found, but no PHY detected.");
    } 
    else 
	{
        NS_FATAL_ERROR("Error: Device is not a valid MmWave device.");
    }

    if (phy == nullptr) 
	{
        NS_FATAL_ERROR("Error: No MmWavePhy found in NetDevice.");
    }

    return phy;
}

/**
 * [Rmove comment] see GetTxRxInfo function: It's mmwave-enb-device for both a and b and sets it as Tx and Rx, make sure to copy the code as it is
 * and just change the internal implementation 
 */
bool MmWave3gppPropagationLossModel::isMediumNTN(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b)
{
	bool ntnMediumFlag = false;

	Ptr<MmWavePhy> phyA = GetMmWavePhy(a);
	Ptr<MmWavePhy> phyB = GetMmWavePhy(b);
	bool BSatelliteModelFlag;
	bool ASatelliteModelFlag;

	// Accessing attributes in MmWavePhy
	if (phyA != nullptr) {
		ASatelliteModelFlag = phyA->GetSatelliteChannelModelFlag();
	}

	if (phyB != nullptr) {
		BSatelliteModelFlag = phyB->GetSatelliteChannelModelFlag();
	}
	
	if (ASatelliteModelFlag || BSatelliteModelFlag)
		ntnMediumFlag = true;

	NS_LOG_DEBUG("NTN medium detected flag: "<<ntnMediumFlag);
	return ntnMediumFlag;
}

double
MmWave3gppPropagationLossModel::ComputePnlos(Ptr<const MobilityModel> a,
                                            Ptr<const MobilityModel> b) const
{
    NS_LOG_FUNCTION(this << a << b);
    // by default returns 1 - PLOS
    return (1 - ComputePlos(a, b));
}

double MmWave3gppPropagationLossModel::ComputePlosNTNDenseUrban(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const
{
	NS_LOG_FUNCTION(this);
	// LOS probability from table 6.6.1-1 of 3GPP 38.811
	std::map<int, double> DenseUrbanLOSProb{
		{10, {28.2}},
		{20, {33.1}},
		{30, {39.8}},
		{40, {46.8}},
		{50, {53.7}},
		{60, {61.2}},
		{70, {73.8}},
		{80, {82.0}},
		{90, {98.1}},
	};

	Vector posA = a->GetPosition();
    Vector posB = b->GetPosition();

    double distance2D = std::sqrt(std::pow(posA.x - posB.x, 2) + std::pow(posA.y - posB.y, 2));

    double heightDifference = posA.z - posB.z;

    double elevationAngleRad = std::atan2(std::abs(heightDifference), distance2D);
    double elevationAngleDeg = RadiansToDegrees(elevationAngleRad);

    // Quantize elevation angle to nearest 10 degrees, within range [10, 90]
    int elevAngleQuantized = std::max(10, std::min(90, static_cast<int>(std::round(elevationAngleDeg / 10.0) * 10)));

    NS_LOG_DEBUG("Elevation Angle: " << elevationAngleDeg << " degrees, Quantized: " << elevAngleQuantized);

    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

	return DenseUrbanLOSProb.at((elevAngleQuantized));
}

double MmWave3gppPropagationLossModel::ComputePlosNTNUrban(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const
{
    // LOS probability from table 6.6.1-1 of 3GPP 38.811
    std::map<int, double> UrbanLOSProb{
        {10, {24.6}},
        {20, {38.6}},
        {30, {49.3}},
        {40, {61.3}},
        {50, {72.6}},
        {60, {80.5}},
        {70, {91.9}},
        {80, {96.8}},
        {90, {99.2}},
    };

	Vector posA = a->GetPosition();
    Vector posB = b->GetPosition();

    double distance2D = std::sqrt(std::pow(posA.x - posB.x, 2) + std::pow(posA.y - posB.y, 2));

    double heightDifference = posA.z - posB.z;

    double elevationAngleRad = std::atan2(std::abs(heightDifference), distance2D);
    double elevationAngleDeg = RadiansToDegrees(elevationAngleRad);

    // Quantize elevation angle to nearest 10 degrees, within range [10, 90]
    int elevAngleQuantized = std::max(10, std::min(90, static_cast<int>(std::round(elevationAngleDeg / 10.0) * 10)));

    NS_LOG_DEBUG("Elevation Angle: " << elevationAngleDeg << " degrees, Quantized: " << elevAngleQuantized);

    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

	return UrbanLOSProb.at((elevAngleQuantized));
}

double MmWave3gppPropagationLossModel::ComputePlosNTNSuburban(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const
{

	// LOS probability from table 6.6.1-1 of 3GPP 38.811
	std::map<int, double> SuburbanLOSProb{
		{10, {78.2}},
		{20, {86.9}},
		{30, {91.9}},
		{40, {92.9}},
		{50, {93.5}},
		{60, {94.0}},
		{70, {94.9}},
		{80, {95.2}},
		{90, {99.8}},
	};

	Vector posA = a->GetPosition();
    Vector posB = b->GetPosition();

    double distance2D = std::sqrt(std::pow(posA.x - posB.x, 2) + std::pow(posA.y - posB.y, 2));

    double heightDifference = posA.z - posB.z;

    double elevationAngleRad = std::atan2(std::abs(heightDifference), distance2D);
    double elevationAngleDeg = RadiansToDegrees(elevationAngleRad);

    // Quantize elevation angle to nearest 10 degrees, within range [10, 90]
    int elevAngleQuantized = std::max(10, std::min(90, static_cast<int>(std::round(elevationAngleDeg / 10.0) * 10)));

    NS_LOG_DEBUG("Elevation Angle: " << elevationAngleDeg << " degrees, Quantized: " << elevAngleQuantized);

    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

	return SuburbanLOSProb.at((elevAngleQuantized));
}

double MmWave3gppPropagationLossModel::ComputePlosNTNRural(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const
{

	// LOS probability from table 6.6.1-1 of 3GPP 38.811
	std::map<int, double> RuralLOSProb{
		{10, {78.2}},
		{20, {86.9}},
		{30, {91.9}},
		{40, {92.9}},
		{50, {93.5}},
		{60, {94.0}},
		{70, {94.9}},
		{80, {95.2}},
		{90, {99.8}},
	};

		Vector posA = a->GetPosition();
    Vector posB = b->GetPosition();

    double distance2D = std::sqrt(std::pow(posA.x - posB.x, 2) + std::pow(posA.y - posB.y, 2));

    double heightDifference = posA.z - posB.z;

    double elevationAngleRad = std::atan2(std::abs(heightDifference), distance2D);
    double elevationAngleDeg = RadiansToDegrees(elevationAngleRad);

    // Quantize elevation angle to nearest 10 degrees, within range [10, 90]
    int elevAngleQuantized = std::max(10, std::min(90, static_cast<int>(std::round(elevationAngleDeg / 10.0) * 10)));

    NS_LOG_DEBUG("Elevation Angle: " << elevationAngleDeg << " degrees, Quantized: " << elevAngleQuantized);

    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

	return RuralLOSProb.at((elevAngleQuantized));
}

double
MmWave3gppPropagationLossModel::ComputePlos(Ptr<const MobilityModel> a,
                                                   Ptr<const MobilityModel> b) const
{

	if (m_ntnScenario == "DenseUrban")
	{
		return ComputePlosNTNDenseUrban(a,b);
	}
	else if (m_ntnScenario == "Urban")
	{
		return ComputePlosNTNUrban(a,b);
	}
	else if (m_ntnScenario == "Suburban")
	{
		return ComputePlosNTNSuburban(a,b);
	}
	else if (m_ntnScenario == "Rural")
	{
		return ComputePlosNTNRural(a,b);
	}
	else
	{
		NS_FATAL_ERROR("Invalid NTN scenario");
		return 1;
	}
}

bool MmWave3gppPropagationLossModel::IsO2iLowPenetrationLoss(channelCondition cond) const
{
    if (cond.m_o2iLowHighCondition == LOW)
    {
        return true;
    }
    else if (cond.m_o2iLowHighCondition == HIGH)
    {
        return false;
    }
    else
    {
        NS_ABORT_MSG("If we have set the O2I condition, we shouldn't be here");
    }
}

double MmWave3gppPropagationLossModel::GetO2iDistance2dIn() const
{
    abort();
}

double MmWave3gppPropagationLossModel::GetO2iLowPenetrationLoss(Ptr<MobilityModel> a, Ptr<MobilityModel> b, channelCondition cond) const
{
    NS_LOG_FUNCTION(this);

    double o2iLossValue = 0;
    double lowLossTw = 0;
    double lossIn = 0;
    double lowlossNormalVariate = 0;
    double lGlass = 0;
    double lConcrete = 0;

    // compute the channel key
    uint32_t key = GetKey(a, b);

    bool notFound = false;     // indicates if the o2iLoss value has not been computed yet
    bool newCondition = false; // indicates if the channel condition has changed

    auto it = m_o2iLossMap.end(); // the o2iLoss map iterator
    if (m_o2iLossMap.find(key) != m_o2iLossMap.end())
    {
        // found the o2iLoss value in the map
        it = m_o2iLossMap.find(key);
        newCondition = (it->second.m_condition != cond.m_channelCondition); // true if the condition changed
    }
    else
    {
        notFound = true;
        // add a new entry in the map and update the iterator
        O2iLossMapItem newItem;
        it = m_o2iLossMap.insert(it, std::make_pair(key, newItem));
    }

    if (notFound || newCondition)
    {
        // distance2dIn is minimum of two independently generated uniformly distributed
        // variables between 0 and 25 m for UMa and UMi-Street Canyon, and between 0 and
        // 10 m for RMa. 2D−in d shall be UT-specifically generated.
        double distance2dIn = GetO2iDistance2dIn();

        // calculate material penetration losses, see TR 38.901 Table 7.4.3-1
        lGlass = 2 + 0.2 * m_frequency / 1e9; // m_frequency is operation frequency in Hz
        lConcrete = 5 + 4 * m_frequency / 1e9;

        lowLossTw =
            5 - 10 * log10(0.3 * std::pow(10, -lGlass / 10) + 0.7 * std::pow(10, -lConcrete / 10));

        // calculate indoor loss
        lossIn = 0.5 * distance2dIn;

        // calculate low loss standard deviation
        lowlossNormalVariate = m_normalO2iLowLossVar->GetValue();

        o2iLossValue = lowLossTw + lossIn + lowlossNormalVariate;
    }
    else
    {
        o2iLossValue = it->second.m_o2iLoss;
    }

    // update the entry in the map
    it->second.m_o2iLoss = o2iLossValue;
    it->second.m_condition = cond.m_channelCondition;

    return o2iLossValue;
}

double
MmWave3gppPropagationLossModel::GetO2iHighPenetrationLoss(
    Ptr<MobilityModel> a,
    Ptr<MobilityModel> b,
    channelCondition cond) const
{
    NS_LOG_FUNCTION(this);

    double o2iLossValue = 0;
    double highLossTw = 0;
    double lossIn = 0;
    double highlossNormalVariate = 0;
    double lIIRGlass = 0;
    double lConcrete = 0;

    // compute the channel key
    uint32_t key = GetKey(a, b);

    bool notFound = false;     // indicates if the o2iLoss value has not been computed yet
    bool newCondition = false; // indicates if the channel condition has changed

    auto it = m_o2iLossMap.end(); // the o2iLoss map iterator
    if (m_o2iLossMap.find(key) != m_o2iLossMap.end())
    {
        // found the o2iLoss value in the map
        it = m_o2iLossMap.find(key);
        newCondition = (it->second.m_condition != cond.m_channelCondition); // true if the condition changed
    }
    else
    {
        notFound = true;
        // add a new entry in the map and update the iterator
        O2iLossMapItem newItem;
        it = m_o2iLossMap.insert(it, std::make_pair(key, newItem));
    }

    if (notFound || newCondition)
    {
        // generate a new independent realization

        // distance2dIn is minimum of two independently generated uniformly distributed
        // variables between 0 and 25 m for UMa and UMi-Street Canyon, and between 0 and
        // 10 m for RMa. 2D−in d shall be UT-specifically generated.
        double distance2dIn = GetO2iDistance2dIn();

        // calculate material penetration losses, see TR 38.901 Table 7.4.3-1
        lIIRGlass = 23 + 0.3 * m_frequency / 1e9;
        lConcrete = 5 + 4 * m_frequency / 1e9;

        highLossTw = 5 - 10 * log10(0.7 * std::pow(10, -lIIRGlass / 10) +
                                    0.3 * std::pow(10, -lConcrete / 10));

        // calculate indoor loss
        lossIn = 0.5 * distance2dIn;

        // calculate low loss standard deviation
        highlossNormalVariate = m_normalO2iHighLossVar->GetValue();

        o2iLossValue = highLossTw + lossIn + highlossNormalVariate;
    }
    else
    {
        o2iLossValue = it->second.m_o2iLoss;
    }

    // update the entry in the map
    it->second.m_o2iLoss = o2iLossValue;
    it->second.m_condition = cond.m_channelCondition;

    return o2iLossValue;
}

O2iConditionValue
MmWave3gppPropagationLossModel::ComputeO2i(Ptr<const MobilityModel> a,
                                          Ptr<const MobilityModel> b) const
{
	double o2iProb = m_uniformVarO2i->GetValue(0, 1);
    if (m_linkO2iConditionToAntennaHeight)
    {
        if (std::min(a->GetPosition().z, b->GetPosition().z) == 1.5)
        {
            return O2O;
        }
        else
        {
            return O2I;
        }
    }
    else
    {
        if (o2iProb < m_o2iThreshold)
        {
            NS_LOG_INFO("Return O2i condition ....");
            return O2I;
        }
        else
        {
            NS_LOG_INFO("Return O2o condition ....");
            return O2O;
        }
    }
}

double
MmWave3gppPropagationLossModel::GetLoss (Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
	double distance3D = a->GetDistanceFrom (b);

	if (distance3D <= 0)
	{
		return  m_minLoss;
	}
	if (distance3D < 3*m_lambda)
	{
		NS_LOG_UNCOND ("distance not within the far field region => inaccurate propagation loss value");
	}

	auto returnParams = GetEnbUePair(a, b);

	if(std::get<2>(returnParams))
	{
		NS_LOG_INFO("Skip pathloss for device to device propagation");
		return 0; // skip the computation for UE to UE channel
	}

	Ptr<MobilityModel> ueMob, enbMob;

	enbMob = std::get<0>(returnParams);
	ueMob =  std::get<1>(returnParams);
	
	

	Vector uePos = ueMob->GetPosition();
	Vector enbPos = enbMob->GetPosition();
	double x = uePos.x-enbPos.x;
	double y = uePos.y-enbPos.y;
	double distance2D = sqrt (x*x +y*y);
	double hBs = enbPos.z;
	double hUt = uePos.z;
	double loss = 0;
	
	channelConditionMap_t::const_iterator it;
	it = m_channelConditionMap.find(std::make_pair(a,b));

	bool ntnMediumFlag = isMediumNTN(a, b);

	if (ntnMediumFlag == true)
	{
		// if (m_ntnScenario == "")
		//NS_LOG_DEBUG("Path loss NTN Scenario: "<<m_ntnScenario);
		if (hBs < 500000)
		{
			NS_FATAL_ERROR("Satellite height should be atleast 500 km TR 38.811");
		}
		channelCondition condition;
		if (it == m_channelConditionMap.end ())
		{
			if (m_channelConditions.compare("l")==0 )
			{
				condition.m_channelCondition = 'l';
			}
			else if (m_channelConditions.compare("n")==0)
			{
				condition.m_channelCondition = 'n';
			}
			else if (m_channelConditions.compare("a")==0)
			{
				// channel condition not found, create new channel

				// compute the LOS probability
				double pLos = ComputePlos(a, b);
				double pNlos = ComputePnlos(a, b);

				// draw a random value
				double pRef = m_uniformVar->GetValue();

				// get the channel condition
				if (pRef <= pLos)
				{
					// LOS
					condition.m_channelCondition = 'l';
				}
				else if (pRef <= pLos + pNlos)
				{
					// NLOS
					condition.m_channelCondition = 'n';
				}
				else
				{
					NS_FATAL_ERROR("invalid channel condition");
				}
				condition.m_o2iCondition = ComputeO2i(a, b);
				if (condition.m_o2iCondition == O2I)
				{
					// Since we have O2I penetration losses, we should choose based on the
					// threshold if it will be low or high penetration losses
					// (see TR38.901 Table 7.4.3)
					double o2iLowHighLossProb = m_uniformO2iLowHighLossVar->GetValue(0, 1);
					
					if (o2iLowHighLossProb < m_o2iLowLossThreshold)
					{
						condition.m_o2iLowHighCondition = LOW;
					}
					else
					{
						condition.m_o2iLowHighCondition = HIGH;
					}
				}
			}

			// assign a large negative value to identify initial transmission.
			condition.m_shadowing = -1e6;
			condition.m_hE = 0;
			//condition.m_carPenetrationLoss = 9+m_norVar.GetValue()*5;
			condition.m_carPenetrationLoss = 10;
			std::pair<channelConditionMap_t::const_iterator, bool> ret;
			ret = m_channelConditionMap.insert (std::make_pair(std::make_pair (a,b), condition));
			m_channelConditionMap.insert (std::make_pair(std::make_pair (b,a), condition));
			it = ret.first;
		}
		
		condition = it->second;

		if (condition.m_channelCondition == 'l')
		{
			loss = GetLossLos(a, b);
		}
		else if (condition.m_channelCondition == 'n')
		{
			loss = GetLossNlos(a, b);
		}
		else
		{
			NS_FATAL_ERROR("Unknown channel condition");
		}

		//shadowing 
		loss += GetShadowing(a,b,condition);

		// [To do] set o2i i2i and nlos parameters
		// get o2i losses
		if (m_buildingPenLossesEnabled &&
			((condition.m_o2iCondition == O2I) ||
			(condition.m_o2iCondition == I2I &&
			condition.m_channelCondition == 'n')))
		{
			if (IsO2iLowPenetrationLoss(condition))
			{
				loss += GetO2iLowPenetrationLoss(a, b, condition);
			}
			else
			{
				loss += GetO2iHighPenetrationLoss(a, b, condition);
			}
		}

		NS_LOG_DEBUG ("NTN Rural scenario, 2D distance = " << distance2D <<"m, LOS = " << condition.m_channelCondition<<", h_BS="<<hBs<<",h_UT="<<hUt <<"Loss: "<<loss);
		return loss;
	}
	else
	{
		if (it == m_channelConditionMap.end ())
		{
			channelCondition condition;

			if (m_channelConditions.compare("l")==0 )
			{
				condition.m_channelCondition = 'l';
				NS_LOG_DEBUG (m_scenario << " scenario, channel condition is fixed to be " << condition.m_channelCondition<<", h_BS="<<hBs<<",h_UT="<<hUt);
			}
			else if (m_channelConditions.compare("n")==0)
			{
				condition.m_channelCondition = 'n';
				NS_LOG_DEBUG (m_scenario << " scenario, channel condition is fixed to be " << condition.m_channelCondition<<", h_BS="<<hBs<<",h_UT="<<hUt);
			}
			else if (m_channelConditions.compare("a")==0)
			{
				double PRef = m_uniformVar->GetValue();
				double probLos;
				//Note: The LOS probability is derived with assuming antenna heights of 3m for indoor, 10m for UMi, and 25m for UMa.
				if (m_scenario == "RMa")
				{
					if(distance2D <= 10)
					{
						probLos = 1;
					}
					else
					{
						probLos = exp(-(distance2D-10)/1000);
					}
				}
				else if (m_scenario == "UMa")
				{
					if(distance2D <= 18)
					{
						probLos = 1;
					}
					else
					{
						double C_hUt;
						if (hUt <= 13)
						{
							C_hUt = 0;
						}
						else if(hUt <=23)
						{
							C_hUt = pow((hUt-13)/10,1.5);
						}
						else
						{
							NS_FATAL_ERROR ("From Table 7.4.2-1, UMa scenario hUT cannot be larger than 23 m");
						}
						probLos = (18/distance2D+exp(-distance2D/63)*(1-18/distance2D))*(1+C_hUt*5/4*pow(distance2D/100,3)*exp(-distance2D/150));
					}
				}
				else if (m_scenario == "UMi-StreetCanyon")
				{
					if(distance2D <= 18)
					{
						probLos = 1;
					}
					else
					{
						probLos = 18/distance2D+exp(-distance2D/36)*(1-18/distance2D);
					}
				}
				else if (m_scenario == "InH-OfficeMixed")
				{
					if(distance2D <= 1.2)
					{
						probLos = 1;
					}
					else if (distance2D <= 6.5)
					{
						probLos = exp(-(distance2D-1.2)/4.7);
					}
					else
					{
						probLos = exp(-(distance2D-6.5)/32.6)*0.32;
					}
				}
				else if (m_scenario == "InH-OfficeOpen")
				{
					if(distance2D <= 5)
					{
						probLos = 1;
					}
					else if (distance2D <= 49)
					{
						probLos = exp(-(distance2D-5)/70.8);
					}
					else
					{
						probLos = exp(-(distance2D-49)/211.7)*0.54;
					}
				}
				else if (m_scenario == "InH-ShoppingMall")
				{
					probLos = 1;

				}
				else
				{
					NS_FATAL_ERROR ("Unknown scenario");
				}

				
				if (PRef <= probLos)
				{
					condition.m_channelCondition = 'l';
					//NS_LOG_DEBUG("  - Decision: PRef <= Prob_LOS → LOS condition selected");
				}
				else
				{
					condition.m_channelCondition = 'n';
					//NS_LOG_DEBUG("  - Decision: PRef > Prob_LOS → NLOS condition selected");
				}
				NS_LOG_DEBUG (m_scenario << " scenario, 2D distance = " << distance2D <<"m, Prob_LOS = " << probLos
						<< ", Prob_REF = " << PRef << ", the channel condition is " << condition.m_channelCondition<<", h_BS="<<hBs<<",h_UT="<<hUt);

			}
			else
			{
				NS_FATAL_ERROR ("Wrong channel condition configuration");
			}
			// assign a large negative value to identify initial transmission.
			condition.m_shadowing = -1e6;
			condition.m_hE = 0;
			//condition.m_carPenetrationLoss = 9+m_norVar->GetValue()*5;
			condition.m_carPenetrationLoss = 10;
			std::pair<channelConditionMap_t::const_iterator, bool> ret;
			ret = m_channelConditionMap.insert (std::make_pair(std::make_pair (a,b), condition));
			m_channelConditionMap.insert (std::make_pair(std::make_pair (b,a), condition));
			it = ret.first;
		}

		/* Reminder.
		* The The LOS NLOS state transition will be implemented in the future as mentioned in secction 7.6.3.3
		* */

		double lossDb = 0;
		double freqGHz = m_frequency/1e9;

		double shadowingStd = 0;
		double shadowingCorDistance = 0;
		if (m_scenario == "RMa")
		{
			if(distance2D < 10)
			{
				NS_LOG_WARN ("The 2D distance is smaller than 10 meters, the 3GPP RMa model may not be accurate");
			}

			if (hBs < 10 || hBs > 150 )
			{
				NS_FATAL_ERROR ("According to table 7.4.1-1, the RMa scenario need to satisfy the following condition, 10 m <= hBS <= 150 m");
			}

			if (hUt < 1 || hUt > 10 )
			{
				NS_FATAL_ERROR ("According to table 7.4.1-1, the RMa scenario need to satisfy the following condition, 1 m <= hUT <= 10 m");
			}
			//default base station antenna height is 35 m
			//hBs = 35;
			//default user antenna height is 1.5 m
			//hUt = 1.5;
			double W = 20; //average street height
			double h = 5; //average building height

			double dBP = 2*M_PI*hBs*hUt*m_frequency/3e8; //break point distance
			double PL1 = 20*log10(40*M_PI*distance3D*freqGHz/3) + std::min(0.03*pow(h,1.72),10.0)*log10(distance3D) - std::min(0.044*pow(h,1.72),14.77) + 0.002*log10(h)*distance3D;

			if(distance2D <= dBP)
			{
				lossDb = PL1;
				shadowingStd = 4;

			}
			else
			{
				//PL2
				lossDb = PL1 + 40*log10(distance3D/dBP);
				shadowingStd= 6;
			}

			switch ((*it).second.m_channelCondition)
			{
				case 'l':
				{
					shadowingCorDistance = 37;
					break;
				}
				case 'n':
				{
					shadowingCorDistance = 120;
					double PLNlos = 161.04-7.1*log10(W)+7.5*log10(h)-(24.37-3.7*pow((h/hBs),2))*log10(hBs)+(43.42-3.1*log10(hBs))*(log10(distance3D)-3)+20*log10(freqGHz)-(3.2*pow(log10(11.75*hUt),2)-4.97);
					lossDb = std::max(PLNlos, lossDb);
					shadowingStd = 8;
					break;
				}
				default:
					NS_FATAL_ERROR ("Programming Error.");
			}

		}
		else if (m_scenario == "UMa")
		{
			if(distance2D < 10)
			{
				NS_LOG_UNCOND ("The 2D distance is smaller than 10 meters, the 3GPP UMa model may not be accurate");
			}

			//default base station value is 25 m
			//hBs = 25;

			if (hUt < 1.5 || hUt > 22.5 )
			{
				NS_FATAL_ERROR ("According to table 7.4.1-1, the UMa scenario need to satisfy the following condition, 1.5 m <= hUT <= 22.5 m");
			}
			//For UMa, the effective environment height should be computed follow Table7.4.1-1.
			if((*it).second.m_hE == 0)
			{
				channelCondition condition;
				condition = (*it).second;
				if (hUt <= 18)
				{
					condition.m_hE = 1;
				}
				else
				{
					double g_d2D = 1.25*pow(distance2D/100,3)*exp(-1*distance2D/150);
					double C_d2D_hUT = pow((hUt-13)/10,1.5)*g_d2D;
					double prob = 1/(1+C_d2D_hUT);

					if(m_uniformVar->GetValue() < prob)
					{
						condition.m_hE = 1;
					}
					else
					{
						int random = m_uniformVar->GetInteger(12, (int)(hUt-1.5));
						condition.m_hE = (double)floor(random/3)*3;
					}
				}
				UpdateConditionMap(a,b,condition);
			}
			double dBP = 4*(hBs-(*it).second.m_hE)*(hUt-(*it).second.m_hE)*m_frequency/3e8;
			if(distance2D <= dBP)
			{
				//PL1
				lossDb = 32.4+20*log10(distance3D)+20*log10(freqGHz);
			}
			else
			{
				//PL2
				double PL2_additional = 40*log10(distance3D)+20*log10(freqGHz)-10*log10(pow(dBP,2)+pow(hBs-hUt,2));
				lossDb = 32.4 + PL2_additional;
			}


			switch ((*it).second.m_channelCondition)
			{
				case 'l':
				{
					shadowingStd = 4;
					shadowingCorDistance = 37;
					break;
				}
				case 'n':
				{
					shadowingCorDistance = 50;
					if(m_optionNlosEnabled)
					{
						//optional propagation loss
						lossDb = 32.4+20*log10(freqGHz)+30*log10(distance3D);
						shadowingStd = 7.8;
					}
					else
					{
						double PLNlos = 13.54+39.08*log10(distance3D)+20*log10(freqGHz)-0.6*(hUt-1.5);
						shadowingStd = 6;
						lossDb = std::max(PLNlos, lossDb);
					}
					break;
				}
				default:
					NS_FATAL_ERROR ("Programming Error.");
			}
		}
		else if (m_scenario == "UMi-StreetCanyon")
		{

			if(distance2D < 10)
			{
				NS_LOG_UNCOND ("The 2D distance is smaller than 10 meters, the 3GPP UMi-StreetCanyon model may not be accurate");
			}

			//default base station value is 10 m
			//hBs = 10;

			if (hUt < 1.5 || hUt > 22.5 )
			{
				NS_FATAL_ERROR ("According to table 7.4.1-1, the UMi-StreetCanyon scenario need to satisfy the following condition, 1.5 m <= hUT <= 22.5 m");
			}
			double dBP = 4*(hBs-1)*(hUt-1)*m_frequency/3e8;
			if(distance2D <= dBP)
			{
				//PL1
				lossDb = 32.4+21*log10(distance3D)+20*log10(freqGHz);
			}
			else
			{
				//PL2
				lossDb = 32.4+40*log10(distance3D)+20*log10(freqGHz)-9.5*log10(pow(dBP,2)+pow(hBs-hUt,2));
			}


			switch ((*it).second.m_channelCondition)
			{
				case 'l':
				{
					shadowingStd = 4;
					shadowingCorDistance = 10;
					break;
				}
				case 'n':
				{
					shadowingCorDistance = 13;
					if(m_optionNlosEnabled)
					{
						//optional propagation loss
						lossDb = 32.4+20*log10(freqGHz)+31.9*log10(distance3D);
						shadowingStd = 8.2;
					}
					else
					{
						double PLNlos = 35.3*log10(distance3D)+22.4+21.3*log10(freqGHz)-0.3*(hUt-1.5);
						shadowingStd = 7.82;
						lossDb = std::max(PLNlos, lossDb);
					}

					break;
				}
				default:
					NS_FATAL_ERROR ("Programming Error.");
			}
		}
		else if (m_scenario == "InH-OfficeMixed" || m_scenario == "InH-OfficeOpen")
		{
			if(distance3D < 1 || distance3D > 100)
			{
				NS_LOG_UNCOND ("The pathloss might not be accurate since 3GPP InH-Office model LoS condition is accurate only within 3D distance between 1 m and 100 m");
			}

			lossDb = 32.4+17.3*log10(distance3D)+20*log10(freqGHz);


			switch ((*it).second.m_channelCondition)
			{
				case 'l':
				{
					shadowingStd = 3;
					shadowingCorDistance = 10;
					break;
				}
				case 'n':
				{
					shadowingCorDistance = 6;
					if(distance3D > 86)
					{
						NS_LOG_UNCOND ("The pathloss might not be accurate since 3GPP InH-Office model NLoS condition only supports 3D distance between 1 m and 86 m");
					}

					if(m_optionNlosEnabled)
					{
						//optional propagation loss
						double PLNlos = 32.4+20*log10(freqGHz)+31.9*log10(distance3D);
						shadowingStd = 8.29;
						lossDb = std::max(PLNlos, lossDb);

					}
					else
					{
						double PLNlos = 38.3*log10(distance3D)+17.3+24.9*log10(freqGHz);
						shadowingStd = 8.03;
						lossDb = std::max(PLNlos, lossDb);
					}
					break;
				}
				default:
					NS_FATAL_ERROR ("Programming Error.");
			}
		}

		else if (m_scenario == "InH-ShoppingMall")
		{
			shadowingCorDistance = 10; //I use the office correlation distance since shopping mall is not in the table.

			if(distance3D < 1 || distance3D > 150)
			{
				NS_LOG_DEBUG ("The pathloss might not be accurate since 3GPP InH-Shopping mall model only supports 3D distance between 1 m and 150 m");\
			}
			lossDb = 32.4+17.3*log10(distance3D)+20*log10(freqGHz);
			shadowingStd = 2;
		}
		else
		{
			NS_FATAL_ERROR ("Unknown channel condition");
		}

		if(m_shadowingEnabled)
		{
			channelCondition cond;
			cond = (*it).second;
			//The first transmission the shadowing is initialed as -1e6,
			//we perform this if check the identify first  transmission.
			if((*it).second.m_shadowing < -1e5)
			{
				double shadowingRandomValue = m_norVar->GetValue();
				cond.m_shadowing = shadowingRandomValue * shadowingStd;
			}
			else
			{
				double deltaX = uePos.x-(*it).second.m_position.x;
				double deltaY = uePos.y-(*it).second.m_position.y;
				double disDiff = sqrt (deltaX*deltaX +deltaY*deltaY);
				
				double R = exp(-1*disDiff/shadowingCorDistance); // from equation 7.4-5.
				
				double previousShadowing = (*it).second.m_shadowing;
				double newRandomComponent = m_norVar->GetValue();
				double uncorrelatedComponent = sqrt(1-R*R)*newRandomComponent*shadowingStd;
				
				
				cond.m_shadowing = R*previousShadowing + uncorrelatedComponent;
			}

			
			lossDb += cond.m_shadowing;
			
			cond.m_position = ueMob->GetPosition();
			UpdateConditionMap(a,b,cond);
		}


		/*FILE* log_file;

		char* fname = (char*)malloc(sizeof(char) * 255);

		memset(fname, 0, sizeof(char) * 255);
		std::string temp;
		if(m_optionNlosEnabled)
		{
			temp = m_scenario+"-"+(*it).second.m_channelCondition+"-opt.txt";
		}
		else
		{
			temp = m_scenario+"-"+(*it).second.m_channelCondition+".txt";
		}

		log_file = fopen(temp.c_str(), "a");

		fprintf(log_file, "%f \t  %f\n", distance3D, lossDb);

		fflush(log_file);

		fclose(log_file);

		if(fname)

		free(fname);

		fname = 0;*/

		if(m_inCar)
		{
			lossDb += (*it).second.m_carPenetrationLoss;
		}

		double finalLoss = std::max (lossDb, m_minLoss);
		
		return finalLoss;
	}


}

uint32_t MmWave3gppPropagationLossModel::GetKey(Ptr<MobilityModel> a, Ptr<MobilityModel> b)
{
    // use the nodes ids to obtain an unique key for the channel between a and b
    // sort the nodes ids so that the key is reciprocal
    uint32_t x1 = std::min(a->GetObject<Node>()->GetId(), b->GetObject<Node>()->GetId());
    uint32_t x2 = std::max(a->GetObject<Node>()->GetId(), b->GetObject<Node>()->GetId());

    // use the cantor function to obtain the key
    uint32_t key = (((x1 + x2) * (x1 + x2 + 1)) / 2) + x2;

    return key;
}

Vector
MmWave3gppPropagationLossModel::GetVectorDifference(Ptr<MobilityModel> a, Ptr<MobilityModel> b)
{
    uint32_t x1 = a->GetObject<Node>()->GetId();
    uint32_t x2 = b->GetObject<Node>()->GetId();

    if (x1 < x2)
    {
        return b->GetPosition() - a->GetPosition();
    }
    else
    {
        return a->GetPosition() - b->GetPosition();
    }
}

double MmWave3gppPropagationLossModel::GetShadowing(Ptr<MobilityModel> a,
                                           Ptr<MobilityModel> b,
                                           channelCondition cond) const
{
    NS_LOG_FUNCTION(this);

    double shadowingValue;

    // compute the channel key
    uint32_t key = GetKey(a, b);

    bool notFound = false;          // indicates if the shadowing value has not been computed yet
    bool newCondition = false;      // indicates if the channel condition has changed
    Vector newDistance;             // the distance vector, that is not a distance but a difference
    auto it = m_shadowingMap.end(); // the shadowing map iterator
    if (m_shadowingMap.find(key) != m_shadowingMap.end())
    {
        // found the shadowing value in the map
        it = m_shadowingMap.find(key);
        newDistance = GetVectorDifference(a, b);
        newCondition = (it->second.m_condition.m_channelCondition != cond.m_channelCondition); // true if the condition changed
    }
    else
    {
        notFound = true;

        // add a new entry in the map and update the iterator
        ShadowingMapItem newItem;
        it = m_shadowingMap.insert(it, std::make_pair(key, newItem));
    }

    if (notFound || newCondition)
    {
        // generate a new independent realization
        shadowingValue = m_normRandomVariable->GetValue() * GetShadowingStd(a, b, cond);
    }
    else
    {
        // compute a new correlated shadowing loss
        Vector2D displacement(newDistance.x - it->second.m_distance.x,
                              newDistance.y - it->second.m_distance.y);
        double R = exp(-1 * displacement.GetLength() / GetShadowingCorrelationDistance(cond));
        shadowingValue = R * it->second.m_shadowing + sqrt(1 - R * R) *
                                                          m_normRandomVariable->GetValue() *
                                                          GetShadowingStd(a, b, cond);
    }

    // update the entry in the map
    it->second.m_shadowing = shadowingValue;
    it->second.m_distance = newDistance; // Save the (0,0,0) vector in case it's the first time we
                                         // are calculating this value
    it->second.m_condition = cond;

    return shadowingValue;
}

int64_t
MmWave3gppPropagationLossModel::DoAssignStreams (int64_t stream)
{
  return 0;
}

void
MmWave3gppPropagationLossModel::UpdateConditionMap (Ptr<MobilityModel> a, Ptr<MobilityModel> b, channelCondition cond) const
{
	m_channelConditionMap[std::make_pair (a,b)] = cond;
	m_channelConditionMap[std::make_pair (b,a)] = cond;

}

char
MmWave3gppPropagationLossModel::GetChannelCondition(Ptr<MobilityModel> a, Ptr<MobilityModel> b)
{
	channelConditionMap_t::const_iterator it;
	it = m_channelConditionMap.find(std::make_pair(a,b));
	if (it == m_channelConditionMap.end ())
	{
		// this is a new link, so we need to compute the channel condition
		GetLoss(a, b);
		it = m_channelConditionMap.find(std::make_pair(a,b));
		if (it == m_channelConditionMap.end ())
		{
			NS_FATAL_ERROR ("Cannot find the link in the map");
		}
	}
	return (*it).second.m_channelCondition;

}

std::string
MmWave3gppPropagationLossModel::GetScenario ()
{
	return m_scenario;
}

void
MmWave3gppPropagationLossModel::SetConfigurationParameters (Ptr<MmWavePhyMacCommon> ptrConfig)
{
	m_phyMacConfig = ptrConfig;
	m_frequency = m_phyMacConfig->GetCenterFrequency();
    static const double C = 299792458.0; // speed of light in vacuum
    m_lambda = C / m_frequency;

    NS_LOG_INFO("Frequency " << m_frequency);
}

/**
 * @brief Computes the free-space path loss using the formula described in 3GPP TR 38.811,
 * Table 6.6.2
 *
 * @param freq the operating frequency
 * @param dist3d the 3D distance between the communicating nodes
 *
 * @return the path loss for NTN scenarios
 */
double
ComputeNtnPathloss(double freq, double dist3d)
{
    return 32.45 + 20 * log10(freq / 1e9) + 20 * log10(dist3d);
};

/**
 * @brief Computes the atmospheric absorption loss using the formula described in 3GPP TR 38.811,
 * Sec 6.6.4
 *
 * @param freq the operating frequency
 * @param elevAngle the elevation angle between the communicating nodes
 *
 * @return the atmospheric absorption loss for NTN scenarios
 */
double
ComputeAtmosphericAbsorptionLoss(double freq, double elevAngle)
{
    double loss = 0;
    if ((elevAngle < 10 && freq > 1e9) || freq >= 10e9)
    {
        int roundedFreq = round(freq / 10e8);
        loss += atmosphericAbsorption[roundedFreq] / sin(elevAngle * (M_PI / 180));
    }

    return loss;
};

/**
 * @brief Computes the ionospheric plus tropospheric scintillation loss using the formulas
 * described in 3GPP TR 38.811, Sec 6.6.6.1-4 and 6.6.6.2, respectively.
 *
 * @param freq the operating frequency
 * @param elevAngleQuantized the quantized elevation angle between the communicating nodes
 *
 * @return the ionospheric plus tropospheric scintillation loss for NTN scenarios
 */
double
ComputeIonosphericPlusTroposphericScintillationLoss(double freq, double elevAngleQuantized)
{
    double loss = 0;
    if (freq < 6e9)
    {
        // Ionospheric
        loss = 6.22 / (pow(freq / 1e9, 1.5));
    }
    else
    {
        // Tropospheric
        loss = troposphericScintillationLoss.at(elevAngleQuantized);
    }
    return loss;
};

/**
 * @brief Computes the clutter loss using the formula
 * described in 3GPP TR 38.811, Sec 6.6.6.1-4 and 6.6.6.2, respectively.
 *
 * @param freq the operating frequency
 * @param elevAngleQuantized the quantized elevation angle between the communicating nodes
 * @param sfcl the nested map containing the Shadow Fading and
 *         Clutter Loss values for the NTN Suburban and Rural scenario
 *
 * @return the clutter loss for NTN scenarios
 */
double
ComputeClutterLoss(double freq,
                   const std::map<int, std::vector<float>>* sfcl,
                   double elevAngleQuantized)
{
    double loss = 0;
    if (freq < 13.0e9)
    {
        loss += (*sfcl).at(elevAngleQuantized)[SFCL_params::S_NLOS_CL]; // Get the Clutter Loss for
                                                                        // the S Band
    }
    else
    {
        loss += (*sfcl).at(elevAngleQuantized)[SFCL_params::Ka_NLOS_CL]; // Get the Clutter Loss for
                                                                         // the Ka Band
    }

    return loss;
};

static const double M_C = 3.0e8; //!< propagation velocity in free space

double MmWave3gppPropagationLossModel::GetLossLosNTNDenseUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_frequency <= 100.0e9,
                  "NTN communications are valid for frequencies between 0.5 and 100 GHz.");

    double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");
    // compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
    double loss = ComputeNtnPathloss(m_frequency, distance3D);

    // Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
    loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

    // Apply Ionospheric plus Tropospheric Scintillation Loss
    loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

    NS_LOG_DEBUG("Loss " << loss);

    return loss;
}

double MmWave3gppPropagationLossModel::GetLossLosNTNUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_frequency <= 100.0e9,
                  "NTN communications are valid for frequencies between 0.5 and 100 GHz.");

    double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");
    // compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
    double loss = ComputeNtnPathloss(m_frequency, distance3D);

    // Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
    loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

    // Apply Ionospheric plus Tropospheric Scintillation Loss
    loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

    NS_LOG_DEBUG("Loss " << loss);

    return loss;
}

double MmWave3gppPropagationLossModel::GetLossLosNTNSuburban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_frequency <= 100.0e9,
                  "NTN communications are valid for frequencies between 0.5 and 100 GHz.");

    double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

	// compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
    double loss = ComputeNtnPathloss(m_frequency, distance3D);

    // Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
    loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

    // Apply Ionospheric plus Tropospheric Scintillation Loss
    loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

    NS_LOG_DEBUG("Loss " << loss);

    return loss;
}

double MmWave3gppPropagationLossModel::GetLossLosNTNRural(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_frequency <= 100.0e9,
                  "NTN communications are valid for frequencies between 0.5 and 100 GHz.");

    double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");
    // compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
    double loss = ComputeNtnPathloss(m_frequency, distance3D);

    // Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
    loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

    // Apply Ionospheric plus Tropospheric Scintillation Loss
    loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

    NS_LOG_DEBUG("Loss " << loss);

    return loss;
}

double
MmWave3gppPropagationLossModel::GetLossLos(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
    NS_LOG_FUNCTION(this);

	if (m_ntnScenario == "DenseUrban")
	{
		return GetLossLosNTNDenseUrban(a,b);
	}
	else if (m_ntnScenario == "Urban")
	{
		return GetLossLosNTNUrban(a,b);
	}
	else if (m_ntnScenario == "Suburban")
	{
		return GetLossLosNTNSuburban(a,b);
	}
	else if (m_ntnScenario == "Rural")
	{
		return GetLossLosNTNRural(a,b);
	}
	else
	{
		NS_FATAL_ERROR("Invalid NTN scenario");
		return 1;
	}	
}

double MmWave3gppPropagationLossModel::GetLossNlosNTNDenseUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
	NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_frequency <= 100.0e9,
		"NTN communications are valid for frequencies between 0.5 and 100 GHz.");

	double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
	// Round the elevation angle into a two-digits integer between 10 and 90, as specified in
	// Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

	int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
	NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
			"Invalid elevation angle!");

	// compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
	double loss = ComputeNtnPathloss(m_frequency, distance3D);

	// Apply Clutter Loss
	loss += ComputeClutterLoss(m_frequency, m_SFCL_DenseUrban, elevAngleQuantized);

	// Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
	loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

	// Apply Ionospheric plus Tropospheric Scintillation Loss
	loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

	NS_LOG_DEBUG("Loss " << loss);
	return loss;
}

double MmWave3gppPropagationLossModel::GetLossNlosNTNUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
	NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_frequency <= 100.0e9,
		"NTN communications are valid for frequencies between 0.5 and 100 GHz.");

	double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
	// Round the elevation angle into a two-digits integer between 10 and 90, as specified in
	// Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

	int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
	NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
			"Invalid elevation angle!");

	// compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
	double loss = ComputeNtnPathloss(m_frequency, distance3D);

	// Apply Clutter Loss
	loss += ComputeClutterLoss(m_frequency, m_SFCL_Urban, elevAngleQuantized);

	// Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
	loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

	// Apply Ionospheric plus Tropospheric Scintillation Loss
	loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

	NS_LOG_DEBUG("Loss " << loss);
	return loss;
}

double MmWave3gppPropagationLossModel::GetLossNlosNTNSuburban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
	NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_frequency <= 100.0e9,
		"NTN communications are valid for frequencies between 0.5 and 100 GHz.");

	double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
	// Round the elevation angle into a two-digits integer between 10 and 90, as specified in
	// Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

	int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
	NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
			"Invalid elevation angle!");
	// compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
	double loss = ComputeNtnPathloss(m_frequency, distance3D);

	// Apply Clutter Loss
	loss += ComputeClutterLoss(m_frequency, m_SFCL_SuburbanRural, elevAngleQuantized);

	// Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
	loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

	// Apply Ionospheric plus Tropospheric Scintillation Loss
	loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

	NS_LOG_DEBUG("Loss " << loss);
	return loss;
}

double MmWave3gppPropagationLossModel::GetLossNlosNTNRural(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
	NS_LOG_FUNCTION(this);

    NS_ASSERT_MSG(m_frequency <= 100.0e9,
		"NTN communications are valid for frequencies between 0.5 and 100 GHz.");

	double distance3D = CalculateDistance(a->GetPosition(), b->GetPosition());

	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
	// Round the elevation angle into a two-digits integer between 10 and 90, as specified in
	// Sec. 6.6.1, 3GPP TR 38.811 v15.4.0

	int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
	NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
			"Invalid elevation angle!");

	// compute the pathloss (see 3GPP TR 38.811, Table 6.6.2)
	double loss = ComputeNtnPathloss(m_frequency, distance3D);

	// Apply Clutter Loss
	loss += ComputeClutterLoss(m_frequency, m_SFCL_SuburbanRural, elevAngleQuantized);

	// Apply Atmospheric Absorption Loss 3GPP 38.811 6.6.4
	loss += ComputeAtmosphericAbsorptionLoss(m_frequency, elevAngle);

	// Apply Ionospheric plus Tropospheric Scintillation Loss
	loss += ComputeIonosphericPlusTroposphericScintillationLoss(m_frequency, elevAngleQuantized);

	NS_LOG_DEBUG("Loss " << loss);
	return loss;
}

double
MmWave3gppPropagationLossModel::GetLossNlos(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
    NS_LOG_FUNCTION(this);

	if (m_ntnScenario == "DenseUrban")
	{
		return GetLossNlosNTNDenseUrban(a,b);
	}
	else if (m_ntnScenario == "Urban")
	{
		return GetLossNlosNTNUrban(a,b);
	}
	else if (m_ntnScenario == "Suburban")
	{
		return GetLossNlosNTNSuburban(a,b);
	}
	else if (m_ntnScenario == "Rural")
	{
		return GetLossNlosNTNRural(a,b);
	}
	else
	{
		NS_FATAL_ERROR("Invalid NTN scenario");
		return 1;
	}
}

double MmWave3gppPropagationLossModel::GetShadowingStdNTNDenseUrban( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const
{
	double shadowingStd;
	NS_LOG_FUNCTION(this);

    std::string freqBand = (m_frequency < 13.0e9) ? "S" : "Ka";
    // auto [elevAngle, elevAngleQuantized] =
    //     ThreeGppChannelConditionModel::GetQuantizedElevationAngle(a, b);
	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0
   
    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

    // Assign Shadowing Standard Deviation according to table 6.6.2-1
    if (cond.m_channelCondition == 'l' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_DenseUrban).at(elevAngleQuantized)[SFCL_params::S_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'l' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_DenseUrban).at(elevAngleQuantized)[SFCL_params::Ka_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_DenseUrban).at(elevAngleQuantized)[SFCL_params::S_NLOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_DenseUrban).at(elevAngleQuantized)[SFCL_params::Ka_NLOS_sigF];
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

	return shadowingStd;
}

double MmWave3gppPropagationLossModel::GetShadowingStdNTNUrban( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const
{
	double shadowingStd;
	NS_LOG_FUNCTION(this);

    std::string freqBand = (m_frequency < 13.0e9) ? "S" : "Ka";
    // auto [elevAngle, elevAngleQuantized] =
    //     ThreeGppChannelConditionModel::GetQuantizedElevationAngle(a, b);
	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0
   
    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

    // Assign Shadowing Standard Deviation according to table 6.6.2-1
    if (cond.m_channelCondition == 'l' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_Urban).at(elevAngleQuantized)[SFCL_params::S_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'l' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_Urban).at(elevAngleQuantized)[SFCL_params::Ka_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_Urban).at(elevAngleQuantized)[SFCL_params::S_NLOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_Urban).at(elevAngleQuantized)[SFCL_params::Ka_NLOS_sigF];
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

	return shadowingStd;
}

double MmWave3gppPropagationLossModel::GetShadowingStdNTNSuburban( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const
{
	double shadowingStd;
	NS_LOG_FUNCTION(this);

    std::string freqBand = (m_frequency < 13.0e9) ? "S" : "Ka";
    // auto [elevAngle, elevAngleQuantized] =
    //     ThreeGppChannelConditionModel::GetQuantizedElevationAngle(a, b);
	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0
   
    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");

    // Assign Shadowing Standard Deviation according to table 6.6.2-1
    if (cond.m_channelCondition == 'l' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::S_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'l' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::Ka_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::S_NLOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::Ka_NLOS_sigF];
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

	return shadowingStd;
}

double MmWave3gppPropagationLossModel::GetShadowingStdNTNRural( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const
{

	double shadowingStd;
	NS_LOG_FUNCTION(this);

    std::string freqBand = (m_frequency < 13.0e9) ? "S" : "Ka";
    // auto [elevAngle, elevAngleQuantized] =
    //     ThreeGppChannelConditionModel::GetQuantizedElevationAngle(a, b);
	double elevAngle = this->GetElevationAngle();//aNTNMob->GetElevationAngle(bNTNMob);
    // Round the elevation angle into a two-digits integer between 10 and 90, as specified in
    // Sec. 6.6.1, 3GPP TR 38.811 v15.4.0
   
    int elevAngleQuantized = (elevAngle < 10) ? 10 : round(elevAngle / 10) * 10;
    NS_ASSERT_MSG((elevAngleQuantized >= 10) && (elevAngleQuantized <= 90),
                  "Invalid elevation angle!");
				  
    // Assign Shadowing Standard Deviation according to table 6.6.2-1
    if (cond.m_channelCondition == 'l' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::S_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'l' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::Ka_LOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "S")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::S_NLOS_sigF];
    }
    else if (cond.m_channelCondition == 'n' && freqBand == "Ka")
    {
        shadowingStd = (*m_SFCL_SuburbanRural).at(elevAngleQuantized)[SFCL_params::Ka_NLOS_sigF];
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

    return shadowingStd;	
}

double
MmWave3gppPropagationLossModel::GetShadowingStd(
    Ptr<MobilityModel> a,
    Ptr<MobilityModel> b,
    channelCondition cond) const
{
    NS_LOG_FUNCTION(this);

	if (m_ntnScenario == "DenseUrban")
	{
		return GetShadowingStdNTNDenseUrban(a,b,cond);
	}
	else if (m_ntnScenario == "Urban")
	{
		return GetShadowingStdNTNUrban(a,b,cond);
	}
	else if (m_ntnScenario == "Suburban")
	{
		return GetShadowingStdNTNSuburban(a,b,cond);
	}
	else if (m_ntnScenario == "Rural")
	{
		return GetShadowingStdNTNRural(a,b,cond);
	}
	else
	{
		NS_FATAL_ERROR("Invalid NTN scenario");
		return 1;
	}
}

double MmWave3gppPropagationLossModel::GetShadowingCorrelationDistanceNTNDenseUrban( channelCondition cond) const
{
	double correlationDistance;
    NS_LOG_FUNCTION(this);
    // See 3GPP TR 38.811, Table 6.7.2-7a/b and Table 6.7.2-8a/b
    if (cond.m_channelCondition == 'l')
    {
        correlationDistance = 37;
    }
    else if (cond.m_channelCondition == 'n')
    {
        correlationDistance = 50;
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

    return correlationDistance;
}

double MmWave3gppPropagationLossModel::GetShadowingCorrelationDistanceNTNUrban( channelCondition cond) const
{
	double correlationDistance;
    NS_LOG_FUNCTION(this);
    // See 3GPP TR 38.811, Table 6.7.2-7a/b and Table 6.7.2-8a/b
    if (cond.m_channelCondition == 'l')
    {
        correlationDistance = 37;
    }
    else if (cond.m_channelCondition == 'n')
    {
        correlationDistance = 50;
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

    return correlationDistance;
}

double MmWave3gppPropagationLossModel::GetShadowingCorrelationDistanceNTNSuburban( channelCondition cond) const
{
	double correlationDistance;
    NS_LOG_FUNCTION(this);
    // See 3GPP TR 38.811, Table 6.7.2-7a/b and Table 6.7.2-8a/b
    if (cond.m_channelCondition == 'l')
    {
        correlationDistance = 37;
    }
    else if (cond.m_channelCondition == 'n')
    {
        correlationDistance = 50;
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

    return correlationDistance;
}
std::string MmWave3gppPropagationLossModel::GetNtnScenario()
{
	return m_ntnScenario;
}

double MmWave3gppPropagationLossModel::GetShadowingCorrelationDistanceNTNRural( channelCondition cond) const
{			
	double correlationDistance;
    NS_LOG_FUNCTION(this);
    // See 3GPP TR 38.811, Table 6.7.2-7a/b and Table 6.7.2-8a/b
    if (cond.m_channelCondition == 'l')
    {
        correlationDistance = 37;
    }
    else if (cond.m_channelCondition == 'n')
    {
        correlationDistance = 120;
    }
    else
    {
        NS_FATAL_ERROR("Unknown channel condition");
    }

    return correlationDistance;
}

double
MmWave3gppPropagationLossModel::GetShadowingCorrelationDistance(
    channelCondition cond) const
{
    NS_LOG_FUNCTION(this);
    
	if (m_ntnScenario == "DenseUrban")
	{
		return GetShadowingCorrelationDistanceNTNDenseUrban(cond);
	}
	else if (m_ntnScenario == "Urban")
	{
		return GetShadowingCorrelationDistanceNTNUrban(cond);
	}
	else if (m_ntnScenario == "Suburban")
	{
		return GetShadowingCorrelationDistanceNTNSuburban(cond);
	}
	else if (m_ntnScenario == "Rural")
	{
		return GetShadowingCorrelationDistanceNTNRural(cond);
	}
	else
	{
		NS_FATAL_ERROR("Invalid NTN scenario");
		return 1;
	}
}
