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
 */


#ifndef MMWAVE_3GPP_PROPAGATION_LOSS_MODEL_H_
#define MMWAVE_3GPP_PROPAGATION_LOSS_MODEL_H_


#include <ns3/propagation-loss-model.h>
#include "ns3/object.h"
#include "ns3/random-variable-stream.h"
#include <ns3/vector.h>
#include <map>
#include "mmwave-phy-mac-common.h"
#include <unordered_map>
/*
 * This 3GPP channel model is implemented base on the 3GPP TR 38.900 v14.1.0 (2016-09).
 *
 * 3rd Generation Partnership Project;
 * Technical Specification Group Radio Access Network;
 * Study on channel model for frequency spectrum above 6 GHz
 * (Release 14)
 *
 * */


using namespace ns3;

/**
 * Possible values for Outdoor to Indoor condition.
 */
enum O2iConditionValue
{
    O2O,   //!< Outdoor to Outdoor
    O2I,   //!< Outdoor to Indoor
    I2I,   //!< Indoor to Indoor
    O2I_ND //!< Outdoor to Indoor condition not defined
};

/**
 * Possible values for Low-High Penetration Loss condition.
 */
enum O2iLowHighConditionValue
{
    LOW,      //!< Low Penetration Losses
    HIGH,     //!< High Penetration Losses
    LH_O2I_ND //!< Low-High Penetration Losses not defined
};

struct channelCondition
{
	  char m_channelCondition;
	  double m_shadowing;
	  Vector m_position;
	  double m_hE; //the effective environment height mentioned in Table 7.4.1-1 Note 1.
	  double m_carPenetrationLoss; //car penetration loss in dB.
    O2iConditionValue m_o2iCondition; //!< contains the information about the O2I state of the channel
    O2iLowHighConditionValue m_o2iLowHighCondition; //!< contains the information about the O2I
                                                    //!< low-high building penetration losses
};

// map store the path loss scenario(LOS,NLOS,OUTAGE) of each propapgation channel
typedef std::map< std::pair< Ptr<MobilityModel>, Ptr<MobilityModel> >, channelCondition> channelConditionMap_t;

class MmWave3gppPropagationLossModel : public PropagationLossModel
{
public:

  static TypeId GetTypeId (void);
  MmWave3gppPropagationLossModel ();

  void SetConfigurationParameters (Ptr<MmWavePhyMacCommon> ptrConfig);

  /**
   * \param minLoss the minimum loss (dB)
   *
   * no matter how short the distance, the total propagation loss (in
   * dB) will always be greater or equal than this value
   */
  void SetMinLoss (double minLoss);

  /**
   * \return the minimum loss.
   */
  double GetMinLoss (void) const;

  /**
   * \returns the current frequency (Hz)
   */
  double GetFrequency (void) const;

  char GetChannelCondition(Ptr<MobilityModel> a, Ptr<MobilityModel> b);

  std::string GetScenario();

  double GetLoss (Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;

  double GetSatelliteLoss (Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;

  /**
   * \brief Returns the shadow fading correlation distance
   * \param cond the LOS/NLOS channel condition
   * \return shadowing correlation distance in meters
   */
  double GetShadowingCorrelationDistance( channelCondition cond) const;
  double GetShadowingCorrelationDistanceNTNDenseUrban( channelCondition cond) const;
  double GetShadowingCorrelationDistanceNTNUrban( channelCondition cond) const;
  double GetShadowingCorrelationDistanceNTNSuburban( channelCondition cond) const;
  double GetShadowingCorrelationDistanceNTNRural( channelCondition cond) const;

    /**
   * \brief Returns the shadow fading standard deviation
   * \param a tx mobility model
   * \param b rx mobility model
   * \param cond the LOS/NLOS channel condition
   * \return shadowing std in dB
   */
  double GetShadowingStd( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const;
  double GetShadowingStdNTNDenseUrban( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const;
  double GetShadowingStdNTNUrban( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const;
  double GetShadowingStdNTNSuburban( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const;
  double GetShadowingStdNTNRural( Ptr<MobilityModel> a,Ptr<MobilityModel> b, channelCondition cond) const;
  
    /**
   * @brief Computes the pathloss between a and b considering that the line of
   *        sight is obstructed
   * @param a tx mobility model
   * @param b rx mobility model
   * @return pathloss value in dB
   */
  double GetLossNlos(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;    
  double GetLossNlosNTNDenseUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;
  double GetLossNlosNTNUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;
  double GetLossNlosNTNSuburban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;
  double GetLossNlosNTNRural(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;

    /**
   * @brief Computes the pathloss between a and b considering that the line of
   *        sight is not obstructed
   * @param a tx mobility model
   * @param b rx mobility model
   * @return pathloss value in dB
   */
  double GetLossLos(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;
  double GetLossLosNTNDenseUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;
  double GetLossLosNTNUrban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;
  double GetLossLosNTNSuburban(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;
  double GetLossLosNTNRural(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;

  double GetShadowing(Ptr<MobilityModel> a, Ptr<MobilityModel> b, channelCondition cond) const;
  static uint32_t  GetKey(Ptr<MobilityModel> a, Ptr<MobilityModel> b);
  static Vector GetVectorDifference(Ptr<MobilityModel> a, Ptr<MobilityModel> b);


  double ComputePnlos(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const;

  double ComputePlos(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const;

  // LOS probability
  double ComputePlosNTNDenseUrban(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const;
  double ComputePlosNTNUrban(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const;
  double ComputePlosNTNSuburban(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const;
  double ComputePlosNTNRural(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const;

  O2iConditionValue ComputeO2i(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b) const;

  bool IsO2iLowPenetrationLoss(channelCondition cond) const;
  double GetO2iLowPenetrationLoss(Ptr<MobilityModel> a, Ptr<MobilityModel> b, channelCondition cond) const;
  double GetO2iDistance2dIn() const;
  double GetO2iHighPenetrationLoss( Ptr<MobilityModel> a, Ptr<MobilityModel> b, channelCondition cond) const;
  static bool isMediumNTN(Ptr<const MobilityModel> a, Ptr<const MobilityModel> b);
  std::string GetNtnScenario();
  private:
  MmWave3gppPropagationLossModel (const MmWave3gppPropagationLossModel &o);
  MmWave3gppPropagationLossModel & operator = (const MmWave3gppPropagationLossModel &o);
  virtual double DoCalcRxPower (double txPowerDbm,
                                Ptr<MobilityModel> a,
                                Ptr<MobilityModel> b) const;
  virtual int64_t DoAssignStreams (int64_t stream);
  void UpdateConditionMap (Ptr<MobilityModel> a, Ptr<MobilityModel> b, channelCondition cond) const;

  std::tuple<Ptr<MobilityModel>, Ptr<MobilityModel>, bool >GetEnbUePair(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;

  double m_lambda;
  double m_frequency;
  double m_minLoss;
  bool m_buildingPenLossesEnabled;                //!< enable/disable building penetration losses
  std::string m_ntnScenario;                      ///< Specific scenario for modelling NTN propogation model

  mutable channelConditionMap_t m_channelConditionMap;
    /** Define a struct for the m_shadowingMap entries */
  struct ShadowingMapItem
  {
      double m_shadowing;                             //!< the shadowing loss in dB
      channelCondition m_condition;                   //!< the LOS/NLOS condition
      Vector m_distance;                              //!< the vector AB
  };  
  mutable std::unordered_map<uint32_t, ShadowingMapItem> m_shadowingMap; //!< map to store the shadowing values


    struct O2iLossMapItem
    {
        double m_o2iLoss;                                //!< the o2i loss in dB
        char m_condition;                               //!< the LOS/NLOS condition
    };

    mutable std::unordered_map<uint32_t, O2iLossMapItem>
        m_o2iLossMap; //!< map to store the o2i Loss values




  Ptr<UniformRandomVariable> m_randomO2iVar1; //!< a uniform random variable for the calculation
                                              //!< of the indoor loss, see TR38.901 Table 7.4.3-2
  Ptr<UniformRandomVariable> m_randomO2iVar2; //!< a uniform random variable for the calculation
                                              //!< of the indoor loss, see TR38.901 Table 7.4.3-2
  Ptr<NormalRandomVariable>
      m_normalO2iLowLossVar; //!< a normal random variable for the calculation of 02i low loss,
                              //!< see TR38.901 Table 7.4.3-2
  Ptr<NormalRandomVariable>
      m_normalO2iHighLossVar; //!< a normal random variable for the calculation of 02i high loss,
                              //!< see TR38.901 Table 7.4.3-2
  Ptr<UniformRandomVariable>
      m_uniformO2iLowHighLossVar; //!< a uniform random variable for the calculation of the
                                  //!< low/high losses, see TR38.901 Table 7.4.3-2
  Ptr<UniformRandomVariable> m_uniformVarO2i; //!< uniform random variable that is used for the
                                              //!< generation of the O2i conditions
                                                
  Ptr<NormalRandomVariable> m_normRandomVariable; //!< normal random variable

    double m_o2iThreshold{
        0}; //!< the threshold for determining what is the ratio of channels with O2I
    double m_o2iLowLossThreshold{0}; //!< the threshold for determining what is the ratio of low -
                                     //!< high O2I building penetration losses
    bool m_linkO2iConditionToAntennaHeight{
        false}; //!< the indicator that determines whether the O2I/O2O condition is determined based
                //!< on the UE height

  std::string m_channelConditions; //limit the channel condition to be LoS/NLoS only.
  std::string m_scenario;
  bool m_optionNlosEnabled;
  Ptr<NormalRandomVariable> m_norVar;
  Ptr<UniformRandomVariable> m_uniformVar;
  bool m_shadowingEnabled;
  bool m_inCar;
  Ptr<MmWavePhyMacCommon> m_phyMacConfig;
};

#endif
