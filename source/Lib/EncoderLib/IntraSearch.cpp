/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2019, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     EncSearch.cpp
 *  \brief    encoder intra search class
 */

#include "IntraSearch.h"

#include "EncModeCtrl.h"

#include "CommonLib/CommonDef.h"
#include "CommonLib/Rom.h"
#include "CommonLib/Picture.h"
#include "CommonLib/UnitTools.h"

#include "CommonLib/dtrace_next.h"
#include "CommonLib/dtrace_buffer.h"

#include <math.h>
#include <limits>

 //! \ingroup EncoderLib
 //! \{

IntraSearch::IntraSearch()
  : m_pSplitCS      (nullptr)
  , m_pFullCS       (nullptr)
  , m_pBestCS       (nullptr)
  , m_pcEncCfg      (nullptr)
  , m_pcTrQuant     (nullptr)
  , m_pcRdCost      (nullptr)
  , m_pcReshape     (nullptr)
  , m_CABACEstimator(nullptr)
  , m_CtxCache      (nullptr)
  , m_isInitialized (false)
{
  for( uint32_t ch = 0; ch < MAX_NUM_TBLOCKS; ch++ )
  {
    m_pSharedPredTransformSkip[ch] = nullptr;
  }
}


void IntraSearch::destroy()
{
  CHECK( !m_isInitialized, "Not initialized" );

  if( m_pcEncCfg )
  {
    const uint32_t uiNumLayersToAllocateSplit = 1;
    const uint32_t uiNumLayersToAllocateFull  = 1;
    const int uiNumSaveLayersToAllocate = 2;

    for( uint32_t layer = 0; layer < uiNumSaveLayersToAllocate; layer++ )
    {
      m_pSaveCS[layer]->destroy();
      delete m_pSaveCS[layer];
    }

    uint32_t numWidths  = gp_sizeIdxInfo->numWidths();
    uint32_t numHeights = gp_sizeIdxInfo->numHeights();

    for( uint32_t width = 0; width < numWidths; width++ )
    {
      for( uint32_t height = 0; height < numHeights; height++ )
      {
        if( gp_sizeIdxInfo->isCuSize( gp_sizeIdxInfo->sizeFrom( width ) ) && gp_sizeIdxInfo->isCuSize( gp_sizeIdxInfo->sizeFrom( height ) ) )
        {
          for( uint32_t layer = 0; layer < uiNumLayersToAllocateSplit; layer++ )
          {
            m_pSplitCS[width][height][layer]->destroy();

            delete m_pSplitCS[width][height][layer];
          }

          for( uint32_t layer = 0; layer < uiNumLayersToAllocateFull; layer++ )
          {
            m_pFullCS[width][height][layer]->destroy();

            delete m_pFullCS[width][height][layer];
          }

          delete[] m_pSplitCS[width][height];
          delete[] m_pFullCS [width][height];

          m_pBestCS[width][height]->destroy();
          m_pTempCS[width][height]->destroy();

          delete m_pTempCS[width][height];
          delete m_pBestCS[width][height];
        }
      }

      delete[] m_pSplitCS[width];
      delete[] m_pFullCS [width];

      delete[] m_pTempCS[width];
      delete[] m_pBestCS[width];
    }

    delete[] m_pSplitCS;
    delete[] m_pFullCS;

    delete[] m_pBestCS;
    delete[] m_pTempCS;

    delete[] m_pSaveCS;
  }

  m_pSplitCS = m_pFullCS = nullptr;

  m_pBestCS = m_pTempCS = nullptr;

  m_pSaveCS = nullptr;

  for( uint32_t ch = 0; ch < MAX_NUM_TBLOCKS; ch++ )
  {
    delete[] m_pSharedPredTransformSkip[ch];
    m_pSharedPredTransformSkip[ch] = nullptr;
  }

  m_tmpStorageLCU.destroy();
  m_isInitialized = false;
}

IntraSearch::~IntraSearch()
{
  if( m_isInitialized )
  {
    destroy();
  }
}

void IntraSearch::init( EncCfg*        pcEncCfg,
                        TrQuant*       pcTrQuant,
                        RdCost*        pcRdCost,
                        CABACWriter*   CABACEstimator,
                        CtxCache*      ctxCache,
                        const uint32_t     maxCUWidth,
                        const uint32_t     maxCUHeight,
                        const uint32_t     maxTotalCUDepth
                       , EncReshape*   pcReshape
)
{
  CHECK(m_isInitialized, "Already initialized");
  m_pcEncCfg                     = pcEncCfg;
  m_pcTrQuant                    = pcTrQuant;
  m_pcRdCost                     = pcRdCost;
  m_CABACEstimator               = CABACEstimator;
  m_CtxCache                     = ctxCache;
  m_pcReshape                    = pcReshape;

  const ChromaFormat cform = pcEncCfg->getChromaFormatIdc();

  IntraPrediction::init( cform, pcEncCfg->getBitDepth( CHANNEL_TYPE_LUMA ) );
  m_tmpStorageLCU.create(UnitArea(cform, Area(0, 0, MAX_CU_SIZE, MAX_CU_SIZE)));

  for( uint32_t ch = 0; ch < MAX_NUM_TBLOCKS; ch++ )
  {
    m_pSharedPredTransformSkip[ch] = new Pel[MAX_CU_SIZE * MAX_CU_SIZE];
  }

  uint32_t numWidths  = gp_sizeIdxInfo->numWidths();
  uint32_t numHeights = gp_sizeIdxInfo->numHeights();

  const uint32_t uiNumLayersToAllocateSplit = 1;
  const uint32_t uiNumLayersToAllocateFull  = 1;

  m_pBestCS = new CodingStructure**[numWidths];
  m_pTempCS = new CodingStructure**[numWidths];

  m_pFullCS  = new CodingStructure***[numWidths];
  m_pSplitCS = new CodingStructure***[numWidths];

  for( uint32_t width = 0; width < numWidths; width++ )
  {
    m_pBestCS[width] = new CodingStructure*[numHeights];
    m_pTempCS[width] = new CodingStructure*[numHeights];

    m_pFullCS [width] = new CodingStructure**[numHeights];
    m_pSplitCS[width] = new CodingStructure**[numHeights];

    for( uint32_t height = 0; height < numHeights; height++ )
    {
      if(  gp_sizeIdxInfo->isCuSize( gp_sizeIdxInfo->sizeFrom( width ) ) && gp_sizeIdxInfo->isCuSize( gp_sizeIdxInfo->sizeFrom( height ) ) )
      {
        m_pBestCS[width][height] = new CodingStructure( m_unitCache.cuCache, m_unitCache.puCache, m_unitCache.tuCache );
        m_pTempCS[width][height] = new CodingStructure( m_unitCache.cuCache, m_unitCache.puCache, m_unitCache.tuCache );

        m_pBestCS[width][height]->create( m_pcEncCfg->getChromaFormatIdc(), Area( 0, 0, gp_sizeIdxInfo->sizeFrom( width ), gp_sizeIdxInfo->sizeFrom( height ) ), false );
        m_pTempCS[width][height]->create( m_pcEncCfg->getChromaFormatIdc(), Area( 0, 0, gp_sizeIdxInfo->sizeFrom( width ), gp_sizeIdxInfo->sizeFrom( height ) ), false );
        m_pFullCS [width][height] = new CodingStructure*[uiNumLayersToAllocateFull];
        m_pSplitCS[width][height] = new CodingStructure*[uiNumLayersToAllocateSplit];

        for( uint32_t layer = 0; layer < uiNumLayersToAllocateFull; layer++ )
        {
          m_pFullCS [width][height][layer] = new CodingStructure( m_unitCache.cuCache, m_unitCache.puCache, m_unitCache.tuCache );

          m_pFullCS [width][height][layer]->create( m_pcEncCfg->getChromaFormatIdc(), Area( 0, 0, gp_sizeIdxInfo->sizeFrom( width ), gp_sizeIdxInfo->sizeFrom( height ) ), false );
        }

        for( uint32_t layer = 0; layer < uiNumLayersToAllocateSplit; layer++ )
        {
          m_pSplitCS[width][height][layer] = new CodingStructure( m_unitCache.cuCache, m_unitCache.puCache, m_unitCache.tuCache );

          m_pSplitCS[width][height][layer]->create( m_pcEncCfg->getChromaFormatIdc(), Area( 0, 0, gp_sizeIdxInfo->sizeFrom( width ), gp_sizeIdxInfo->sizeFrom( height ) ), false );
        }
      }
      else
      {
        m_pBestCS[width][height] = nullptr;
        m_pTempCS[width][height] = nullptr;

        m_pFullCS [width][height] = nullptr;
        m_pSplitCS[width][height] = nullptr;
      }
    }
  }

  const int uiNumSaveLayersToAllocate = 2;

  m_pSaveCS = new CodingStructure*[uiNumSaveLayersToAllocate];

  for( uint32_t depth = 0; depth < uiNumSaveLayersToAllocate; depth++ )
  {
    m_pSaveCS[depth] = new CodingStructure( m_unitCache.cuCache, m_unitCache.puCache, m_unitCache.tuCache );
    m_pSaveCS[depth]->create( UnitArea( cform, Area( 0, 0, maxCUWidth, maxCUHeight ) ), false );
  }

  m_isInitialized = true;
}


//////////////////////////////////////////////////////////////////////////
// INTRA PREDICTION
//////////////////////////////////////////////////////////////////////////

bool IntraSearch::estIntraPredLumaQT( CodingUnit &cu, Partitioner &partitioner, const double bestCostSoFar, bool mtsCheckRangeFlag, int mtsFirstCheckId, int mtsLastCheckId, bool moreProbMTSIdxFirst )
{
  CodingStructure       &cs            = *cu.cs;
  const SPS             &sps           = *cs.sps;
  const uint32_t             uiWidthBit    = g_aucLog2[partitioner.currArea().lwidth() ];
  const uint32_t             uiHeightBit   =                   g_aucLog2[partitioner.currArea().lheight()];

  // Lambda calculation at equivalent Qp of 4 is recommended because at that Qp, the quantization divisor is 1.
  const double sqrtLambdaForFirstPass = m_pcRdCost->getMotionLambda(cu.transQuantBypass) / double(1 << SCALE_BITS);


  //===== loop over partitions =====

  const TempCtx ctxStart          ( m_CtxCache, m_CABACEstimator->getCtx() );
  const TempCtx ctxStartMipFlag    ( m_CtxCache, SubCtx( Ctx::MipFlag,          m_CABACEstimator->getCtx() ) );
  const TempCtx ctxStartMipMode    ( m_CtxCache, SubCtx( Ctx::MipMode,          m_CABACEstimator->getCtx() ) );
  const TempCtx ctxStartIspMode    ( m_CtxCache, SubCtx( Ctx::ISPMode,          m_CABACEstimator->getCtx() ) );
  const TempCtx ctxStartPlanarFlag ( m_CtxCache, SubCtx( Ctx::IntraLumaPlanarFlag, m_CABACEstimator->getCtx() ) );
  const TempCtx ctxStartIntraMode(m_CtxCache, SubCtx(Ctx::IntraLumaMpmFlag, m_CABACEstimator->getCtx()));
  const TempCtx ctxStartMrlIdx      ( m_CtxCache, SubCtx( Ctx::MultiRefLineIdx,        m_CABACEstimator->getCtx() ) );

  CHECK( !cu.firstPU, "CU has no PUs" );
  const bool keepResi   = cs.pps->getPpsRangeExtension().getCrossComponentPredictionEnabledFlag() || KEEP_PRED_AND_RESI_SIGNALS;

  // variables for saving fast intra modes scan results across multiple LFNST passes
  bool LFNSTLoadFlag = sps.getUseLFNST() && cu.lfnstIdx != 0;
  bool LFNSTSaveFlag = sps.getUseLFNST() && cu.lfnstIdx == 0;

  LFNSTSaveFlag &= sps.getUseIntraMTS() ? cu.mtsFlag == 0 : true;

  const uint32_t lfnstIdx = cu.lfnstIdx;


  const int width  = partitioner.currArea().lwidth();
  const int height = partitioner.currArea().lheight();

  // Marking MTS usage for faster MTS
  // 0: MTS is either not applicable for current CU (cuWidth > MTS_INTRA_MAX_CU_SIZE or cuHeight > MTS_INTRA_MAX_CU_SIZE), not active in the config file or the fast decision algorithm is not used in this case
  // 1: MTS fast algorithm can be applied for the current CU, and the DCT2 is being checked
  // 2: MTS is being checked for current CU. Stored results of DCT2 can be utilized for speedup
  uint8_t mtsUsageFlag = 0;
  const int maxSizeEMT = MTS_INTRA_MAX_CU_SIZE;
  if( width <= maxSizeEMT && height <= maxSizeEMT && sps.getUseIntraMTS() )
  {
    mtsUsageFlag = ( sps.getUseLFNST() && cu.mtsFlag == 1 ) ? 2 : 1;
  }

  if( width * height < 64 && !m_pcEncCfg->getUseFastLFNST() )
  {
    mtsUsageFlag = 0;
  }
#if INCLUDE_ISP_CFG_FLAG
  int nOptionsForISP = ( sps.getUseISP() && cu.mtsFlag == 0 && cu.lfnstIdx == 0 ) ? NUM_INTRA_SUBPARTITIONS_MODES : 1;
#else
  int nOptionsForISP = ( cu.mtsFlag == 0 && cu.lfnstIdx == 0 ) ? NUM_INTRA_SUBPARTITIONS_MODES : 1;
#endif
  double bestCurrentCost = bestCostSoFar;

  int ispOptions[NUM_INTRA_SUBPARTITIONS_MODES] = { 0 };
  if( nOptionsForISP > 1 )
  {
#if MAX_TB_SIZE_SIGNALLING
    auto splitsThatCanBeUsedForISP = CU::canUseISPSplit( width, height, cu.cs->sps->getMaxTbSize() );
#else
    auto splitsThatCanBeUsedForISP = CU::canUseISPSplit( width, height, MAX_TB_SIZEY );
#endif
    if( splitsThatCanBeUsedForISP == CAN_USE_VER_AND_HORL_SPLITS )
    {
      const CodingUnit* cuLeft  = cu.ispMode != NOT_INTRA_SUBPARTITIONS ? cs.getCU( cs.area.blocks[partitioner.chType].pos().offset( -1, 0 ), partitioner.chType ) : nullptr;
      const CodingUnit* cuAbove = cu.ispMode != NOT_INTRA_SUBPARTITIONS ? cs.getCU( cs.area.blocks[partitioner.chType].pos().offset( 0, -1 ), partitioner.chType ) : nullptr;
      bool ispHorIsFirstTest = CU::firstTestISPHorSplit( width, height, COMPONENT_Y, cuLeft, cuAbove );
      if( ispHorIsFirstTest )
      {
        ispOptions[1] = HOR_INTRA_SUBPARTITIONS;
        ispOptions[2] = VER_INTRA_SUBPARTITIONS;
      }
      else
      {
        ispOptions[1] = VER_INTRA_SUBPARTITIONS;
        ispOptions[2] = HOR_INTRA_SUBPARTITIONS;
      }
    }
    else if( splitsThatCanBeUsedForISP == HOR_INTRA_SUBPARTITIONS )
    {
      nOptionsForISP = 2;
      ispOptions[1] = HOR_INTRA_SUBPARTITIONS;
    }
    else if( splitsThatCanBeUsedForISP == VER_INTRA_SUBPARTITIONS )
    {
      nOptionsForISP = 2;
      ispOptions[1] = VER_INTRA_SUBPARTITIONS;
    }
    else
    {
      nOptionsForISP = 1;
    }
  }
  if( nOptionsForISP > 1 )
  {
    //variables for the full RD list without MRL modes
    m_rdModeListWithoutMrl      .clear();
    m_rdModeListWithoutMrlHor   .clear();
    m_rdModeListWithoutMrlVer   .clear();
    //variables with data from regular intra used to skip ISP splits
    m_intraModeDiagRatio        .clear();
    m_intraModeHorVerRatio      .clear();
    m_intraModeTestedNormalIntra.clear();
  }

  const bool testBDPCM = m_pcEncCfg->getRDPCM() && CU::bdpcmAllowed( cu, ComponentID( partitioner.chType ) ) && cu.mtsFlag == 0 && cu.lfnstIdx == 0;
  static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> uiHadModeList;
  static_vector<double, FAST_UDI_MAX_RDMODE_NUM> CandCostList;
  static_vector<double, FAST_UDI_MAX_RDMODE_NUM> CandHadList;


  auto &pu = *cu.firstPU;
  bool validReturn = false;
  {
    CandHadList.clear();
    CandCostList.clear();
    uiHadModeList.clear();

    CHECK(pu.cu != &cu, "PU is not contained in the CU");

    //===== determine set of modes to be tested (using prediction signal only) =====
    int numModesAvailable = NUM_LUMA_MODE; // total number of Intra modes
    const bool fastMip    = sps.getUseMIP() && m_pcEncCfg->getUseFastMIP();
    const bool mipAllowed = sps.getUseMIP() && ( cu.lfnstIdx == 0 ) && isLuma( partitioner.chType ) && pu.lwidth() <= MIP_MAX_WIDTH && pu.lheight() <= MIP_MAX_HEIGHT;
    const bool testMip    = mipAllowed && mipModesAvailable( pu.Y() ) && !(fastMip && (cu.lwidth() > 2 * cu.lheight() || cu.lheight() > 2 * cu.lwidth()));

    static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> uiRdModeList;

    int numModesForFullRD = 3;
    numModesForFullRD = g_aucIntraModeNumFast_UseMPM_2D[uiWidthBit - MIN_CU_LOG2][uiHeightBit - MIN_CU_LOG2];

#if INTRA_FULL_SEARCH
    numModesForFullRD = numModesAvailable;
#endif

    if( mtsUsageFlag != 2 )
    {
      // this should always be true
      CHECK( !pu.Y().valid(), "PU is not valid" );
#if ENABLE_JVET_L0283_MRL
      bool isFirstLineOfCtu = (((pu.block(COMPONENT_Y).y)&((pu.cs->sps)->getMaxCUWidth() - 1)) == 0);
      int numOfPassesExtendRef = (isFirstLineOfCtu ? 1 : MRL_NUM_REF_LINES);
#endif
      pu.multiRefIdx = 0;

      if( numModesForFullRD != numModesAvailable )
      {
        CHECK( numModesForFullRD >= numModesAvailable, "Too many modes for full RD search" );

        const CompArea &area = pu.Y();

        PelBuf piOrg         = cs.getOrgBuf(area);
        PelBuf piPred        = cs.getPredBuf(area);

        DistParam distParamSad;
        DistParam distParamHad;
        if (cu.slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag())
        {
          CompArea      tmpArea(COMPONENT_Y, area.chromaFormat, Position(0, 0), area.size());
          PelBuf tmpOrg = m_tmpStorageLCU.getBuf(tmpArea);
          tmpOrg.copyFrom(piOrg);
          tmpOrg.rspSignal(m_pcReshape->getFwdLUT());
          m_pcRdCost->setDistParam(distParamSad, tmpOrg, piPred, sps.getBitDepth(CHANNEL_TYPE_LUMA), COMPONENT_Y, false); // Use SAD cost
          m_pcRdCost->setDistParam(distParamHad, tmpOrg, piPred, sps.getBitDepth(CHANNEL_TYPE_LUMA), COMPONENT_Y,  true); // Use HAD (SATD) cost
        }
        else
        {
          m_pcRdCost->setDistParam(distParamSad, piOrg, piPred, sps.getBitDepth(CHANNEL_TYPE_LUMA), COMPONENT_Y, false); // Use SAD cost
          m_pcRdCost->setDistParam(distParamHad, piOrg, piPred, sps.getBitDepth(CHANNEL_TYPE_LUMA), COMPONENT_Y,  true); // Use HAD (SATD) cost
        }

        distParamSad.applyWeight = false;
        distParamHad.applyWeight = false;

        if( testMip)
        {
          numModesForFullRD += fastMip? std::max(2, g_aucLog2[std::min(pu.lwidth(), pu.lheight())] - 1) : numModesForFullRD;
        }
        const int numHadCand = (testMip ? 2 : 1) * 3;

        //*** Derive (regular) candidates using Hadamard
        cu.mipFlag = false;

        //===== init pattern for luma prediction =====
        initIntraPatternChType(cu, pu.Y(), true);
        bool bSatdChecked[NUM_INTRA_MODE];
        memset( bSatdChecked, 0, sizeof( bSatdChecked ) );

        if( !LFNSTLoadFlag )
        {
          for( int modeIdx = 0; modeIdx < numModesAvailable; modeIdx++ )
          {
            uint32_t       uiMode = modeIdx;
            Distortion minSadHad = 0;

            // Skip checking extended Angular modes in the first round of SATD
            if( uiMode > DC_IDX && ( uiMode & 1 ) )
            {
              continue;
            }

            bSatdChecked[uiMode] = true;

            pu.intraDir[0] = modeIdx;

            initPredIntraParams(pu, pu.Y(), sps);
            if( useDPCMForFirstPassIntraEstimation( pu, uiMode ) )
            {
              encPredIntraDPCM( COMPONENT_Y, piOrg, piPred, uiMode );
            }
            else
            {
              predIntraAng( COMPONENT_Y, piPred, pu);
            }
            // Use the min between SAD and HAD as the cost criterion
            // SAD is scaled by 2 to align with the scaling of HAD
            minSadHad += std::min(distParamSad.distFunc(distParamSad)*2, distParamHad.distFunc(distParamHad));

            // NB xFracModeBitsIntra will not affect the mode for chroma that may have already been pre-estimated.
            m_CABACEstimator->getCtx() = SubCtx( Ctx::MipFlag, ctxStartMipFlag );
            m_CABACEstimator->getCtx() = SubCtx( Ctx::ISPMode, ctxStartIspMode );
            m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaPlanarFlag, ctxStartPlanarFlag);
            m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaMpmFlag, ctxStartIntraMode);
            m_CABACEstimator->getCtx() = SubCtx( Ctx::MultiRefLineIdx, ctxStartMrlIdx );

            uint64_t fracModeBits = xFracModeBitsIntra(pu, uiMode, CHANNEL_TYPE_LUMA);

            double cost = ( double ) minSadHad + (double)fracModeBits * sqrtLambdaForFirstPass;

            DTRACE(g_trace_ctx, D_INTRA_COST, "IntraHAD: %u, %llu, %f (%d)\n", minSadHad, fracModeBits, cost, uiMode);

            updateCandList( ModeInfo(false, 0, NOT_INTRA_SUBPARTITIONS, uiMode), cost,          uiRdModeList,  CandCostList, numModesForFullRD );
            updateCandList( ModeInfo(false, 0, NOT_INTRA_SUBPARTITIONS, uiMode), (double)minSadHad, uiHadModeList, CandHadList,  numHadCand );
          }
          if( LFNSTSaveFlag )
          {
            // save found best modes
            m_uiSavedNumRdModesLFNST   = numModesForFullRD;
            m_uiSavedRdModeListLFNST   = uiRdModeList;
            m_dSavedModeCostLFNST      = CandCostList;
            // PBINTRA fast
            m_uiSavedHadModeListLFNST  = uiHadModeList;
            m_dSavedHadListLFNST       = CandHadList;
            LFNSTSaveFlag              = false;
          }
        } // NSSTFlag
        else
        {
          // restore saved modes
          numModesForFullRD = m_uiSavedNumRdModesLFNST;
          uiRdModeList      = m_uiSavedRdModeListLFNST;
          CandCostList      = m_dSavedModeCostLFNST;
          // PBINTRA fast
          uiHadModeList     = m_uiSavedHadModeListLFNST;
          CandHadList       = m_dSavedHadListLFNST;

          LFNSTLoadFlag     = false;
        } // !LFNSTFlag

        CHECK( uiRdModeList.size() != numModesForFullRD, "Error: RD mode list size" );
        static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> parentCandList = uiRdModeList;

        // Second round of SATD for extended Angular modes
        for (int modeIdx = 0; modeIdx < numModesForFullRD; modeIdx++)
        {
          unsigned parentMode = parentCandList[modeIdx].modeId;
          if (parentMode > (DC_IDX + 1) && parentMode < (NUM_LUMA_MODE - 1))
          {
            for (int subModeIdx = -1; subModeIdx <= 1; subModeIdx += 2)
            {
              unsigned mode = parentMode + subModeIdx;


              if (!bSatdChecked[mode])
              {
                pu.intraDir[0] = mode;

                initPredIntraParams(pu, pu.Y(), sps);
                if (useDPCMForFirstPassIntraEstimation(pu, mode))
                {
                  encPredIntraDPCM(COMPONENT_Y, piOrg, piPred, mode);
                }
                else
                {
                  predIntraAng(COMPONENT_Y, piPred, pu );
                }

                // Use the min between SAD and SATD as the cost criterion
                // SAD is scaled by 2 to align with the scaling of HAD
                Distortion minSadHad = std::min(distParamSad.distFunc(distParamSad)*2, distParamHad.distFunc(distParamHad));

                // NB xFracModeBitsIntra will not affect the mode for chroma that may have already been pre-estimated.
                m_CABACEstimator->getCtx() = SubCtx( Ctx::MipFlag, ctxStartMipFlag );
                m_CABACEstimator->getCtx() = SubCtx( Ctx::ISPMode, ctxStartIspMode );
                m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaPlanarFlag, ctxStartPlanarFlag);
                m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaMpmFlag, ctxStartIntraMode);
                m_CABACEstimator->getCtx() = SubCtx( Ctx::MultiRefLineIdx, ctxStartMrlIdx );

                uint64_t fracModeBits = xFracModeBitsIntra(pu, mode, CHANNEL_TYPE_LUMA);

                double cost = (double) minSadHad + (double) fracModeBits * sqrtLambdaForFirstPass;

                updateCandList( ModeInfo( false, 0, NOT_INTRA_SUBPARTITIONS, mode ), cost,        uiRdModeList,  CandCostList, numModesForFullRD );
                updateCandList( ModeInfo( false, 0, NOT_INTRA_SUBPARTITIONS, mode ), (double)minSadHad, uiHadModeList, CandHadList,  numHadCand );

                bSatdChecked[mode] = true;
              }
            }
          }
        }
        if( nOptionsForISP > 1 )
        {
          //we save the list with no mrl modes to keep only the Hadamard selected modes (no mpms)
          m_rdModeListWithoutMrl = uiRdModeList;
        }
#if ENABLE_JVET_L0283_MRL
        pu.multiRefIdx = 1;
        const int  numMPMs = NUM_MOST_PROBABLE_MODES;
        unsigned  multiRefMPM [numMPMs];
        PU::getIntraMPMs(pu, multiRefMPM);
        for (int mRefNum = 1; mRefNum < numOfPassesExtendRef; mRefNum++)
        {
          int multiRefIdx = MULTI_REF_LINE_IDX[mRefNum];

          pu.multiRefIdx = multiRefIdx;
          {
            initIntraPatternChType(cu, pu.Y(), true);
          }
          for (int x = 1; x < numMPMs; x++)
          {
            uint32_t mode = multiRefMPM[x];
            {
              pu.intraDir[0] = mode;
              initPredIntraParams(pu, pu.Y(), sps);

              if (useDPCMForFirstPassIntraEstimation(pu, mode))
              {
                encPredIntraDPCM(COMPONENT_Y, piOrg, piPred, mode);
              }
              else
              {
                predIntraAng(COMPONENT_Y, piPred, pu);
              }

              // Use the min between SAD and SATD as the cost criterion
              // SAD is scaled by 2 to align with the scaling of HAD
              Distortion minSadHad = std::min(distParamSad.distFunc(distParamSad)*2, distParamHad.distFunc(distParamHad));

              // NB xFracModeBitsIntra will not affect the mode for chroma that may have already been pre-estimated.
              m_CABACEstimator->getCtx() = SubCtx( Ctx::MipFlag, ctxStartMipFlag );
              m_CABACEstimator->getCtx() = SubCtx( Ctx::ISPMode, ctxStartIspMode );
              m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaPlanarFlag, ctxStartPlanarFlag);
              m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaMpmFlag, ctxStartIntraMode);
              m_CABACEstimator->getCtx() = SubCtx( Ctx::MultiRefLineIdx, ctxStartMrlIdx );

              uint64_t fracModeBits = xFracModeBitsIntra(pu, mode, CHANNEL_TYPE_LUMA);

              double cost = (double)minSadHad + (double)fracModeBits * sqrtLambdaForFirstPass;
              updateCandList( ModeInfo( false, multiRefIdx, NOT_INTRA_SUBPARTITIONS, mode ), cost,        uiRdModeList,  CandCostList, numModesForFullRD );
              updateCandList( ModeInfo( false, multiRefIdx, NOT_INTRA_SUBPARTITIONS, mode ), (double)minSadHad, uiHadModeList, CandHadList,  numHadCand );
            }
          }
        }
#endif
        CHECKD( uiRdModeList.size() != numModesForFullRD, "Error: RD mode list size" );

          //*** Derive MIP candidates using Hadamard
          if (testMip)
          {
            cu.mipFlag = true;
            pu.multiRefIdx = 0;

            initIntraMip( pu );

            for (uint32_t uiMode = 0; uiMode < getNumModesMip(pu.Y()); uiMode++)
            {
              pu.intraDir[CHANNEL_TYPE_LUMA] = uiMode;
              predIntraMip(COMPONENT_Y, piPred, pu);

              // Use the min between SAD and HAD as the cost criterion
              // SAD is scaled by 2 to align with the scaling of HAD
              Distortion minSadHad = std::min(distParamSad.distFunc(distParamSad)*2, distParamHad.distFunc(distParamHad));

              m_CABACEstimator->getCtx() = SubCtx( Ctx::MipFlag, ctxStartMipFlag );
              m_CABACEstimator->getCtx() = SubCtx( Ctx::MipMode, ctxStartMipMode );

              uint64_t fracModeBits = xFracModeBitsIntra(pu, uiMode, CHANNEL_TYPE_LUMA);

              double cost = double(minSadHad) + double(fracModeBits) * sqrtLambdaForFirstPass;

              updateCandList(ModeInfo(true, 0, NOT_INTRA_SUBPARTITIONS, uiMode),        cost,  uiRdModeList,  CandCostList, numModesForFullRD);
              updateCandList(ModeInfo(true, 0, NOT_INTRA_SUBPARTITIONS, uiMode), double(minSadHad), uiHadModeList, CandHadList, numHadCand);
            }

            const double thresholdHadCost = 1.0 + 1.4 / sqrt((double)(pu.lwidth()*pu.lheight()));
            reduceHadCandList(uiRdModeList, CandCostList, numModesForFullRD, thresholdHadCost, 0.0);
          }

        if( m_pcEncCfg->getFastUDIUseMPMEnabled() )
        {
          const int numMPMs = NUM_MOST_PROBABLE_MODES;
          unsigned  uiPreds[numMPMs];

          pu.multiRefIdx = 0;

          const int numCand = PU::getIntraMPMs( pu, uiPreds );

          for( int j = 0; j < numCand; j++ )
          {
            bool mostProbableModeIncluded = false;
            ModeInfo mostProbableMode( false, 0, NOT_INTRA_SUBPARTITIONS, uiPreds[j] );


            for( int i = 0; i < numModesForFullRD; i++ )
            {
              mostProbableModeIncluded |= ( mostProbableMode == uiRdModeList[i] );
            }
            if( !mostProbableModeIncluded )
            {
              numModesForFullRD++;
              uiRdModeList.push_back( mostProbableMode );
              CandCostList.push_back(0);
            }
          }
          if( nOptionsForISP > 1 )
          {
            //we add the ISP MPMs to the list without mrl modes
            m_rdModeListWithoutMrlHor = m_rdModeListWithoutMrl;
            m_rdModeListWithoutMrlVer = m_rdModeListWithoutMrl;
            for (int k = 0; k < m_rdModeListWithoutMrl.size(); k++)
            {
              m_rdModeListWithoutMrlHor[k].ispMod = HOR_INTRA_SUBPARTITIONS;
              m_rdModeListWithoutMrlVer[k].ispMod = VER_INTRA_SUBPARTITIONS;
            }
            static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM>* listPointer;
            for( int k = 1; k < nOptionsForISP; k++ )
            {
              cu.ispMode = ispOptions[k];
              listPointer = &( cu.ispMode == HOR_INTRA_SUBPARTITIONS ? m_rdModeListWithoutMrlHor : m_rdModeListWithoutMrlVer );
              const int numCandISP = PU::getIntraMPMs( pu, uiPreds );
              for( int j = 0; j < numCandISP; j++ )
              {
                bool mostProbableModeIncluded = false;
                ModeInfo mostProbableMode( false, 0, cu.ispMode, uiPreds[j] );

                for( int i = 0; i < listPointer->size(); i++ )
                {
                  mostProbableModeIncluded |= ( mostProbableMode == listPointer->at( i ) );
                }
                if( !mostProbableModeIncluded )
                {
                  listPointer->push_back( mostProbableMode );
                }
              }
            }
            cu.ispMode = NOT_INTRA_SUBPARTITIONS;
          }
        }
        //*** Add MPMs for MIP to candidate list
        if (!fastMip && testMip && pu.lwidth() < 8 && pu.lheight() < 8)
        {
          unsigned mpm[NUM_MPM_MIP];
          int numCandMip = PU::getMipMPMs(pu, mpm);

          for( int j = 0; j < numCandMip; j++ )
          {
            bool mostProbableModeIncluded = false;
            ModeInfo mostProbableMode(true, 0, NOT_INTRA_SUBPARTITIONS, mpm[j]);
            for( int i = 0; i < numModesForFullRD; i++ )
            {
              mostProbableModeIncluded |= (mostProbableMode == uiRdModeList[i]);
            }
            if( !mostProbableModeIncluded )
            {
              numModesForFullRD++;
              uiRdModeList.push_back( mostProbableMode );
              CandCostList.push_back(0);
            }
          }
        }
      }
      else
      {
        THROW( "Full search not supported for MIP" );
      }
      if( sps.getUseLFNST() && mtsUsageFlag == 1 )
      {
        // Store the modes to be checked with RD
        m_savedNumRdModes[ lfnstIdx ]     = numModesForFullRD;
        std::copy_n( uiRdModeList.begin(),  numModesForFullRD, m_savedRdModeList[ lfnstIdx ] );
      }
    }
    else //mtsUsage = 2 (here we potentially reduce the number of modes that will be full-RD checked)
    {
      if( m_pcEncCfg->getUseFastLFNST() || !cu.slice->isIntra() )
      {
        numModesForFullRD = 0;

        double thresholdSkipMode = 1.0 + ( ( cu.lfnstIdx > 0 ) ? 0.1 : 1.0 ) * ( 1.4 / sqrt( ( double ) ( width*height ) ) );

        // Skip checking the modes with much larger R-D cost than the best mode
        for( int i = 0; i < m_savedNumRdModes[ lfnstIdx ]; i++ )
        {
          if( m_modeCostStore[ lfnstIdx ][ i ] <= thresholdSkipMode * m_bestModeCostStore[ lfnstIdx ] )
          {
            uiRdModeList.push_back( m_savedRdModeList[ lfnstIdx ][ i ] );
            numModesForFullRD++;
          }
        }
      }
      else //this is necessary because we skip the candidates list calculation, since it was already obtained for the DCT-II. Now we load it
      {
        // Restore the modes to be checked with RD
        numModesForFullRD = m_savedNumRdModes[ lfnstIdx ];
        uiRdModeList.resize( numModesForFullRD );
        std::copy_n( m_savedRdModeList[ lfnstIdx ], m_savedNumRdModes[ lfnstIdx ], uiRdModeList.begin() );
        CandCostList.resize( numModesForFullRD );
      }
    }

    if( nOptionsForISP > 1 ) // we remove the non-MPMs from the ISP lists
    {
      static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> uiRdModeListCopyHor = m_rdModeListWithoutMrlHor;
      m_rdModeListWithoutMrlHor.clear();
      static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> uiRdModeListCopyVer = m_rdModeListWithoutMrlVer;
      m_rdModeListWithoutMrlVer.clear();
      static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> *listPointerCopy, *listPointer;
      for( int ispOptionIdx = 1; ispOptionIdx < nOptionsForISP; ispOptionIdx++ )
      {
        cu.ispMode = ispOptions[ispOptionIdx];
        //we get the mpm cand list
        const int numMPMs = NUM_MOST_PROBABLE_MODES;
        unsigned  uiPreds[numMPMs];

        pu.multiRefIdx = 0;

        PU::getIntraMPMs( pu, uiPreds );

        //we copy only the ISP MPMs
        listPointerCopy = &( cu.ispMode == HOR_INTRA_SUBPARTITIONS ? uiRdModeListCopyHor : uiRdModeListCopyVer );
        listPointer     = &( cu.ispMode == HOR_INTRA_SUBPARTITIONS ? m_rdModeListWithoutMrlHor : m_rdModeListWithoutMrlVer );
        for( int k = 0; k < listPointerCopy->size(); k++ )
        {
          for( int q = 0; q < numMPMs; q++ )
          {
            if (listPointerCopy->at(k) == ModeInfo( false, 0, cu.ispMode, uiPreds[q] ))
            {
              listPointer->push_back( listPointerCopy->at( k ) );
              break;
            }
          }
        }
      }
      cu.ispMode = NOT_INTRA_SUBPARTITIONS;
    }


    CHECK( numModesForFullRD != uiRdModeList.size(), "Inconsistent state!" );

    // after this point, don't use numModesForFullRD

    // PBINTRA fast
    if( m_pcEncCfg->getUsePbIntraFast() && !cs.slice->isIntra() && uiRdModeList.size() < numModesAvailable && !cs.slice->getDisableSATDForRD() && ( mtsUsageFlag != 2 || lfnstIdx > 0 ) )
    {
      double pbintraRatio = (lfnstIdx > 0) ? 1.25 : PBINTRA_RATIO;
      int maxSize = -1;
      const int numHadCand = (testMip ? 2 : 1) * 3;
      for (int k = numHadCand - 1; k >= 0; k--)
      {
        if (CandHadList.size() < (k + 1) || CandHadList[k] > cs.interHad * pbintraRatio) { maxSize = k; }
      }
      if (maxSize > 0)
      {
        uiRdModeList.resize(std::min<size_t>(uiRdModeList.size(), maxSize));
        if (nOptionsForISP > 1)
        {
          m_rdModeListWithoutMrlHor.resize(std::min<size_t>(m_rdModeListWithoutMrlHor.size(), maxSize));
          m_rdModeListWithoutMrlVer.resize(std::min<size_t>(m_rdModeListWithoutMrlVer.size(), maxSize));
        }
      }
      if (maxSize == 0)
      {
        cs.dist = std::numeric_limits<Distortion>::max();
        cs.interHad = 0;

        //===== reset context models =====
        m_CABACEstimator->getCtx() = SubCtx(Ctx::MipFlag, ctxStartMipFlag);
        m_CABACEstimator->getCtx() = SubCtx(Ctx::MipMode, ctxStartMipMode);
        m_CABACEstimator->getCtx() = SubCtx(Ctx::ISPMode, ctxStartIspMode);
        m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaPlanarFlag, ctxStartPlanarFlag);
        m_CABACEstimator->getCtx() = SubCtx(Ctx::IntraLumaMpmFlag, ctxStartIntraMode);
        m_CABACEstimator->getCtx() = SubCtx(Ctx::MultiRefLineIdx, ctxStartMrlIdx);

        return false;
      }
    }

    if ( nOptionsForISP > 1 )
    {
      //we create a single full RD list that includes all intra modes using regular intra, MRL and ISP
      auto* firstIspList  = ispOptions[1] == HOR_INTRA_SUBPARTITIONS ? &m_rdModeListWithoutMrlHor : &m_rdModeListWithoutMrlVer;
      auto* secondIspList = ispOptions[1] == HOR_INTRA_SUBPARTITIONS ? &m_rdModeListWithoutMrlVer : &m_rdModeListWithoutMrlHor;

      if( !sps.getUseLFNST() && m_pcEncCfg->getUseFastISP() )
      {
        CHECKD( uiRdModeList.size() > CandCostList.size(), "Error: CandCostList size" );
        // find the first non-MRL, non-MIP mode
        int indexFirstMode = int(uiRdModeList.size()) - 1; // default is last mode
        for (int k = 0; k < int(uiRdModeList.size()); k++)
        {
          if (uiRdModeList[k].mRefId == 0 && uiRdModeList[k].mipFlg == false)
          {
            indexFirstMode = k;
            break;
          }
        }
        // move the mode indicated by indexFirstMode to the beginning
        for (int idx = indexFirstMode - 1; idx >= 0; idx--)
        {
          std::swap(uiRdModeList[idx], uiRdModeList[idx + 1]);
          std::swap(CandCostList[idx], CandCostList[idx + 1]);
        }
        //insert all ISP modes after the first non-mrl mode
        uiRdModeList.insert(uiRdModeList.begin() + 1, secondIspList->begin(), secondIspList->end());
        uiRdModeList.insert(uiRdModeList.begin() + 1, firstIspList->begin(), firstIspList->end());
      }
      else
      {
        //insert all ISP modes at the end of the current list
        uiRdModeList.insert( uiRdModeList.end(), secondIspList->begin(), secondIspList->end() );
        uiRdModeList.insert( uiRdModeList.end(), firstIspList->begin() , firstIspList->end()  );
      }
    }

    //===== check modes (using r-d costs) =====
    ModeInfo       uiBestPUMode;
    int            bestBDPCMMode = 0;
    double         bestCostNonBDPCM = MAX_DOUBLE;

    CodingStructure *csTemp = m_pTempCS[gp_sizeIdxInfo->idxFrom( cu.lwidth() )][gp_sizeIdxInfo->idxFrom( cu.lheight() )];
    CodingStructure *csBest = m_pBestCS[gp_sizeIdxInfo->idxFrom( cu.lwidth() )][gp_sizeIdxInfo->idxFrom( cu.lheight() )];

    csTemp->slice = cs.slice;
    csBest->slice = cs.slice;
    csTemp->initStructData();
    csBest->initStructData();

    m_bestCostNonMip = MAX_DOUBLE;
    static_vector<int, FAST_UDI_MAX_RDMODE_NUM> rdModeIdxList;
    if (testMip)
    {
    static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> uiRdModeListTemp;
    for( int i = 0; i < uiRdModeList.size(); i++)
    {
      if( !uiRdModeList[i].mipFlg && uiRdModeList[i].ispMod==NOT_INTRA_SUBPARTITIONS )
      {
        uiRdModeListTemp.push_back( uiRdModeList[i] );
        rdModeIdxList.push_back( i );
      }
    }
    for( int i = 0; i < uiRdModeList.size(); i++)
    {
      if( uiRdModeList[i].mipFlg || uiRdModeList[i].ispMod!=NOT_INTRA_SUBPARTITIONS )
      {
        uiRdModeListTemp.push_back( uiRdModeList[i] );
        rdModeIdxList.push_back( i );
      }
    }
    for( int i = 0; i < uiRdModeList.size(); i++)
    {
      uiRdModeList[i] = uiRdModeListTemp[i];
    }
    }
    // just to be sure
    numModesForFullRD = ( int ) uiRdModeList.size();
    PartSplit intraSubPartitionsProcOrder = TU_NO_ISP;
    int       bestNormalIntraModeIndex    = -1;
    TUIntraSubPartitioner subTuPartitioner( partitioner );
    if( !cu.ispMode && !cu.mtsFlag )
    {
      m_modeCtrl->setMtsFirstPassNoIspCost( MAX_DOUBLE );
    }
    bool      ispHorAllZeroCbfs = false, ispVerAllZeroCbfs = false;

    for (int mode = -2 * int(testBDPCM); mode < (int)uiRdModeList.size(); mode++)
    {
      // set CU/PU to luma prediction mode
      ModeInfo uiOrgMode;
      if ( mode < 0 )
      {
        cu.bdpcmMode = -mode;

#if JVET_O0315_RDPCM_INTRAMODE_ALIGN
        uiOrgMode = ModeInfo(false, 0, NOT_INTRA_SUBPARTITIONS, cu.bdpcmMode == 2 ? VER_IDX : HOR_IDX);
#else
        unsigned mpm_pred[NUM_MOST_PROBABLE_MODES];
        PU::getIntraMPMs(pu, mpm_pred);
        uiOrgMode = ModeInfo(false, 0, NOT_INTRA_SUBPARTITIONS, mpm_pred[0]);
#endif
        cu.mipFlag                     = uiOrgMode.mipFlg;
        cu.ispMode                     = uiOrgMode.ispMod;
        pu.multiRefIdx                 = uiOrgMode.mRefId;
        pu.intraDir[CHANNEL_TYPE_LUMA] = uiOrgMode.modeId;
      }
      else
      {
        cu.bdpcmMode = 0;
        uiOrgMode = uiRdModeList[mode];
      cu.mipFlag                     = uiOrgMode.mipFlg;
      cu.ispMode                     = uiOrgMode.ispMod;
      pu.multiRefIdx                 = uiOrgMode.mRefId;
      pu.intraDir[CHANNEL_TYPE_LUMA] = uiOrgMode.modeId;

      CHECK(cu.mipFlag && pu.multiRefIdx, "Error: combination of MIP and MRL not supported");
      CHECK(pu.multiRefIdx && (pu.intraDir[0] == PLANAR_IDX), "Error: combination of MRL and Planar mode not supported");
      CHECK(cu.ispMode && cu.mipFlag, "Error: combination of ISP and MIP not supported");
      CHECK(cu.ispMode && pu.multiRefIdx, "Error: combination of ISP and MRL not supported");
        if( cu.ispMode )
        {
          intraSubPartitionsProcOrder = CU::getISPType( cu, COMPONENT_Y );
          bool tuIsDividedInRows = CU::divideTuInRows( cu );
          if ( ( tuIsDividedInRows && ispHorAllZeroCbfs ) || ( !tuIsDividedInRows && ispVerAllZeroCbfs ) )
          {
            continue;
          }
          if( m_intraModeDiagRatio.at( bestNormalIntraModeIndex ) > 1.25 )
          {
            continue;
          }
          if( ( m_intraModeHorVerRatio.at( bestNormalIntraModeIndex ) > 1.25 && tuIsDividedInRows ) || ( m_intraModeHorVerRatio.at( bestNormalIntraModeIndex ) < 0.8 && !tuIsDividedInRows ) )
          {
            continue;
          }
        }
      }

      // set context models
      m_CABACEstimator->getCtx() = ctxStart;

      // determine residual for partition
      cs.initSubStructure( *csTemp, partitioner.chType, cs.area, true );

      bool tmpValidReturn = false;
      if( cu.ispMode )
      {
        tmpValidReturn = xRecurIntraCodingLumaQT( *csTemp, subTuPartitioner, bestCurrentCost, 0, intraSubPartitionsProcOrder, false,
                                                  mtsCheckRangeFlag, mtsFirstCheckId, mtsLastCheckId, moreProbMTSIdxFirst );
      }
      else
      {
        if( ! fastMip )
        {
          m_bestCostNonMip = MAX_DOUBLE;
        }
        tmpValidReturn = xRecurIntraCodingLumaQT( *csTemp, partitioner, uiBestPUMode.ispMod ? bestCurrentCost : MAX_DOUBLE, -1, TU_NO_ISP, uiBestPUMode.ispMod,
                                                  mtsCheckRangeFlag, mtsFirstCheckId, mtsLastCheckId, moreProbMTSIdxFirst );
      }

      if( cu.ispMode && !csTemp->cus[0]->firstTU->cbf[COMPONENT_Y] )
      {
        if( !sps.getUseLFNST() )
        {
          if ( cu.ispMode == HOR_INTRA_SUBPARTITIONS )
          {
            ispHorAllZeroCbfs |= ( m_pcEncCfg->getUseFastISP() && csTemp->tus[0]->lheight() > 2 && csTemp->cost >= bestCurrentCost );
          }
          else
          {
            ispVerAllZeroCbfs |= ( m_pcEncCfg->getUseFastISP() && csTemp->tus[0]->lwidth() > 2 && csTemp->cost >= bestCurrentCost );
          }
        }
        csTemp->cost = MAX_DOUBLE;
        csTemp->costDbOffset = 0;
        tmpValidReturn = false;
      }
      validReturn |= tmpValidReturn;

      if( sps.getUseLFNST() && mtsUsageFlag == 1 && !cu.ispMode && mode >= 0 )
      {
        m_modeCostStore[ lfnstIdx ][ testMip ? rdModeIdxList[ mode ] : mode ] = tmpValidReturn ? csTemp->cost : ( MAX_DOUBLE / 2.0 ); //(MAX_DOUBLE / 2.0) ??
      }


      DTRACE( g_trace_ctx, D_INTRA_COST, "IntraCost T %f (%d) \n", csTemp->cost, uiOrgMode.modeId );


      if( tmpValidReturn )
      {
        // check r-d cost
        if( csTemp->cost < csBest->cost )
        {
          std::swap( csTemp, csBest );

          uiBestPUMode  = uiOrgMode;
          bestBDPCMMode = cu.bdpcmMode;
          if( sps.getUseLFNST() && mtsUsageFlag == 1 && !cu.ispMode )
          {
            m_bestModeCostStore[ lfnstIdx ] = csBest->cost; //cs.cost;
          }
          if( csBest->cost < bestCurrentCost )
          {
            bestCurrentCost = csBest->cost;
          }
          if( !cu.ispMode && !cu.mtsFlag )
          {
            m_modeCtrl->setMtsFirstPassNoIspCost( csBest->cost );
          }
        }
        if( !cu.ispMode && !cu.bdpcmMode && csBest->cost < bestCostNonBDPCM )
        {
          bestCostNonBDPCM = csBest->cost;
          bestNormalIntraModeIndex = mode;
        }
      }

      csTemp->releaseIntermediateData();
    } // Mode loop
    cu.ispMode = uiBestPUMode.ispMod;

    if( validReturn )
    {
      cs.useSubStructure( *csBest, partitioner.chType, pu.singleChan( CHANNEL_TYPE_LUMA ), true, true, keepResi, keepResi );
    }
    csBest->releaseIntermediateData();
    if( validReturn )
    {
      //=== update PU data ====
      cu.mipFlag = uiBestPUMode.mipFlg;
      pu.multiRefIdx = uiBestPUMode.mRefId;
      pu.intraDir[ CHANNEL_TYPE_LUMA ] = uiBestPUMode.modeId;
      cu.bdpcmMode = bestBDPCMMode;
    }
  }

  //===== reset context models =====
  m_CABACEstimator->getCtx() = ctxStart;

  return validReturn;
}

void IntraSearch::estIntraPredChromaQT( CodingUnit &cu, Partitioner &partitioner, const double maxCostAllowed )
{
  const ChromaFormat format   = cu.chromaFormat;
  const uint32_t    numberValidComponents = getNumberValidComponents(format);
  CodingStructure &cs = *cu.cs;
  const TempCtx ctxStart  ( m_CtxCache, m_CABACEstimator->getCtx() );

  cs.setDecomp( cs.area.Cb(), false );

  double    bestCostSoFar = maxCostAllowed;
  bool      lumaUsesISP   = !CS::isDualITree( *cu.cs ) && cu.ispMode;
  PartSplit ispType       = lumaUsesISP ? CU::getISPType( cu, COMPONENT_Y ) : TU_NO_ISP;
  CHECK( cu.ispMode && bestCostSoFar < 0, "bestCostSoFar must be positive!" );

  auto &pu = *cu.firstPU;

  {
    uint32_t       uiBestMode = 0;
    Distortion uiBestDist = 0;
    double     dBestCost = MAX_DOUBLE;

    //----- init mode list ----
    {
      uint32_t  uiMinMode = 0;
      uint32_t  uiMaxMode = NUM_CHROMA_MODE;

      //----- check chroma modes -----
      uint32_t chromaCandModes[ NUM_CHROMA_MODE ];
      PU::getIntraChromaCandModes( pu, chromaCandModes );

      // create a temporary CS
      CodingStructure &saveCS = *m_pSaveCS[0];
      saveCS.pcv      = cs.pcv;
      saveCS.picture  = cs.picture;
      saveCS.area.repositionTo( cs.area );
      saveCS.clearTUs();

      if( !CS::isDualITree( cs ) && cu.ispMode )
      {
        saveCS.clearCUs();
        saveCS.clearPUs();
      }

      if( CS::isDualITree( cs ) )
      {
        if( partitioner.canSplit( TU_MAX_TR_SPLIT, cs ) )
        {
          partitioner.splitCurrArea( TU_MAX_TR_SPLIT, cs );

          do
          {
            cs.addTU( CS::getArea( cs, partitioner.currArea(), partitioner.chType ), partitioner.chType ).depth = partitioner.currTrDepth;
          } while( partitioner.nextPart( cs ) );

          partitioner.exitCurrSplit();
        }
        else
        cs.addTU( CS::getArea( cs, partitioner.currArea(), partitioner.chType ), partitioner.chType );
      }

      std::vector<TransformUnit*> orgTUs;

      if( lumaUsesISP )
      {
        CodingUnit& auxCU = saveCS.addCU( cu, partitioner.chType );
        auxCU.ispMode = cu.ispMode;
        saveCS.sps = cu.cs->sps;
        saveCS.addPU( *cu.firstPU, partitioner.chType );
      }


      // create a store for the TUs
      for( const auto &ptu : cs.tus )
      {
        // for split TUs in HEVC, add the TUs without Chroma parts for correct setting of Cbfs
        if( lumaUsesISP || pu.contains( *ptu, CHANNEL_TYPE_CHROMA ) )
        {
          saveCS.addTU( *ptu, partitioner.chType );
          orgTUs.push_back( ptu );
        }
      }
      if( lumaUsesISP )
      {
        saveCS.clearCUs();
      }
      // SATD pre-selecting.
      int satdModeList[NUM_CHROMA_MODE];
      int64_t satdSortedCost[NUM_CHROMA_MODE];
      for (int i = 0; i < NUM_CHROMA_MODE; i++)
      {
        satdSortedCost[i] = 0; // for the mode not pre-select by SATD, do RDO by default, so set the initial value 0.
        satdModeList[i] = 0;
      }
      bool modeIsEnable[NUM_INTRA_MODE + 1]; // use intra mode idx to check whether enable
      for (int i = 0; i < NUM_INTRA_MODE + 1; i++)
      {
        modeIsEnable[i] = 1;
      }

      DistParam distParam;
      const bool useHadamard = !cu.transQuantBypass;
      pu.intraDir[1] = MDLM_L_IDX; // temporary assigned, just to indicate this is a MDLM mode. for luma down-sampling operation.

      initIntraPatternChType(cu, pu.Cb());
      initIntraPatternChType(cu, pu.Cr());
      xGetLumaRecPixels(pu, pu.Cb());

      for (int idx = uiMinMode; idx <= uiMaxMode - 1; idx++)
      {
        int mode = chromaCandModes[idx];
        satdModeList[idx] = mode;
        if (PU::isLMCMode(mode) && !PU::isLMCModeEnabled(pu, mode))
        {
          continue;
        }
        if ((mode == LM_CHROMA_IDX) || (mode == PLANAR_IDX) || (mode == DM_CHROMA_IDX)) // only pre-check regular modes and MDLM modes, not including DM ,Planar, and LM
        {
          continue;
        }
        pu.intraDir[1] = mode; // temporary assigned, for SATD checking.

        int64_t sad = 0;
        CodingStructure& cs = *(pu.cs);

        CompArea areaCb = pu.Cb();
        PelBuf orgCb = cs.getOrgBuf(areaCb);
        PelBuf predCb = cs.getPredBuf(areaCb);

        m_pcRdCost->setDistParam(distParam, orgCb, predCb, pu.cs->sps->getBitDepth(CHANNEL_TYPE_CHROMA), COMPONENT_Cb, useHadamard);
        distParam.applyWeight = false;

        if (PU::isLMCMode(mode))
        {
          predIntraChromaLM(COMPONENT_Cb, predCb, pu, areaCb, mode);
        }
        else
        {
          initPredIntraParams(pu, pu.Cb(), *pu.cs->sps);
          predIntraAng(COMPONENT_Cb, predCb, pu);
        }

        sad += distParam.distFunc(distParam);

        CompArea areaCr = pu.Cr();
        PelBuf orgCr = cs.getOrgBuf(areaCr);
        PelBuf predCr = cs.getPredBuf(areaCr);

        m_pcRdCost->setDistParam(distParam, orgCr, predCr, pu.cs->sps->getBitDepth(CHANNEL_TYPE_CHROMA), COMPONENT_Cr, useHadamard);
        distParam.applyWeight = false;

        if (PU::isLMCMode(mode))
        {
          predIntraChromaLM(COMPONENT_Cr, predCr, pu, areaCr, mode);
        }
        else
        {
          initPredIntraParams(pu, pu.Cr(), *pu.cs->sps);
          predIntraAng(COMPONENT_Cr, predCr, pu);
        }
        sad += distParam.distFunc(distParam);
        satdSortedCost[idx] = sad;
      }
      // sort the mode based on the cost from small to large.
      int tempIdx = 0;
      int64_t tempCost = 0;
      for (int i = uiMinMode; i <= uiMaxMode - 1; i++)
      {
        for (int j = i + 1; j <= uiMaxMode - 1; j++)
        {
          if (satdSortedCost[j] < satdSortedCost[i])
          {
            tempIdx = satdModeList[i];
            satdModeList[i] = satdModeList[j];
            satdModeList[j] = tempIdx;

            tempCost = satdSortedCost[i];
            satdSortedCost[i] = satdSortedCost[j];
            satdSortedCost[j] = tempCost;

          }
        }
      }
      int reducedModeNumber = 2; // reduce the number of chroma modes
      for (int i = 0; i < reducedModeNumber; i++)
      {
        modeIsEnable[satdModeList[uiMaxMode - 1 - i]] = 0; // disable the last reducedModeNumber modes
      }

      // save the dist
      Distortion baseDist = cs.dist;

      for (uint32_t uiMode = uiMinMode; uiMode < uiMaxMode; uiMode++)
      {
        const int chromaIntraMode = chromaCandModes[uiMode];
        if( PU::isLMCMode( chromaIntraMode ) && ! PU::isLMCModeEnabled( pu, chromaIntraMode ) )
        {
          continue;
        }
        if (!modeIsEnable[chromaIntraMode] && PU::isLMCModeEnabled(pu, chromaIntraMode)) // when CCLM is disable, then MDLM is disable. not use satd checking
        {
          continue;
        }
        cs.setDecomp( pu.Cb(), false );
        cs.dist = baseDist;
        //----- restore context models -----
        m_CABACEstimator->getCtx() = ctxStart;

        //----- chroma coding -----
        pu.intraDir[1] = chromaIntraMode;

        xRecurIntraChromaCodingQT( cs, partitioner, bestCostSoFar, ispType );
        if( lumaUsesISP && cs.dist == MAX_UINT )
        {
          continue;
        }

        if (cs.pps->getUseTransformSkip())
        {
          m_CABACEstimator->getCtx() = ctxStart;
        }

        uint64_t fracBits   = xGetIntraFracBitsQT( cs, partitioner, false, true, -1, ispType );
        Distortion uiDist = cs.dist;
        double    dCost   = m_pcRdCost->calcRdCost( fracBits, uiDist - baseDist );

        //----- compare -----
        if( dCost < dBestCost )
        {
          if( lumaUsesISP && dCost < bestCostSoFar )
          {
            bestCostSoFar = dCost;
          }
          for( uint32_t i = getFirstComponentOfChannel( CHANNEL_TYPE_CHROMA ); i < numberValidComponents; i++ )
          {
            const CompArea &area = pu.blocks[i];

            saveCS.getRecoBuf     ( area ).copyFrom( cs.getRecoBuf   ( area ) );
#if KEEP_PRED_AND_RESI_SIGNALS
            saveCS.getPredBuf     ( area ).copyFrom( cs.getPredBuf   ( area ) );
            saveCS.getResiBuf     ( area ).copyFrom( cs.getResiBuf   ( area ) );
#endif
            saveCS.getPredBuf     ( area ).copyFrom( cs.getPredBuf   (area ) );
            cs.picture->getPredBuf( area ).copyFrom( cs.getPredBuf   (area ) );
            cs.picture->getRecoBuf( area ).copyFrom( cs.getRecoBuf( area ) );

            for( uint32_t j = 0; j < saveCS.tus.size(); j++ )
            {
              saveCS.tus[j]->copyComponentFrom( *orgTUs[j], area.compID );
            }
          }

          dBestCost  = dCost;
          uiBestDist = uiDist;
          uiBestMode = chromaIntraMode;
        }
      }

      for( uint32_t i = getFirstComponentOfChannel( CHANNEL_TYPE_CHROMA ); i < numberValidComponents; i++ )
      {
        const CompArea &area = pu.blocks[i];

        cs.getRecoBuf         ( area ).copyFrom( saveCS.getRecoBuf( area ) );
#if KEEP_PRED_AND_RESI_SIGNALS
        cs.getPredBuf         ( area ).copyFrom( saveCS.getPredBuf( area ) );
        cs.getResiBuf         ( area ).copyFrom( saveCS.getResiBuf( area ) );
#endif
        cs.getPredBuf         ( area ).copyFrom( saveCS.getPredBuf( area ) );
        cs.picture->getPredBuf( area ).copyFrom( cs.getPredBuf    ( area ) );

        cs.picture->getRecoBuf( area ).copyFrom( cs.    getRecoBuf( area ) );

        for( uint32_t j = 0; j < saveCS.tus.size(); j++ )
        {
          orgTUs[ j ]->copyComponentFrom( *saveCS.tus[ j ], area.compID );
        }
      }
    }

    pu.intraDir[1] = uiBestMode;
    cs.dist        = uiBestDist;
  }

  //----- restore context models -----
  m_CABACEstimator->getCtx() = ctxStart;
  if( lumaUsesISP && bestCostSoFar >= maxCostAllowed )
  {
    cu.ispMode = 0;
  }
}

void IntraSearch::IPCMSearch(CodingStructure &cs, Partitioner& partitioner)
{
  ComponentID compStr = (CS::isDualITree(cs) && !isLuma(partitioner.chType)) ? COMPONENT_Cb: COMPONENT_Y;
  ComponentID compEnd = (CS::isDualITree(cs) && isLuma(partitioner.chType)) ? COMPONENT_Y : COMPONENT_Cr;
  for( ComponentID compID = compStr; compID <= compEnd; compID = ComponentID(compID+1) )
  {

    xEncPCM(cs, partitioner, compID);
  }

  cs.getPredBuf().fill(0);
  cs.getResiBuf().fill(0);
  cs.getOrgResiBuf().fill(0);

  cs.dist     = 0;
  cs.fracBits = 0;
  cs.cost     = 0;

  cs.setDecomp(cs.area);
  cs.picture->getPredBuf(cs.area).copyFrom(cs.getPredBuf());
}

void IntraSearch::xEncPCM(CodingStructure &cs, Partitioner& partitioner, const ComponentID &compID)
{
  TransformUnit &tu = *cs.getTU( partitioner.chType );

  const int  channelBitDepth = cs.sps->getBitDepth(toChannelType(compID));
  const uint32_t uiPCMBitDepth = cs.sps->getPCMBitDepth(toChannelType(compID));

  const int pcmShiftRight = (channelBitDepth - int(uiPCMBitDepth));

  CompArea  area    = tu.blocks[compID];
  PelBuf    pcmBuf  = tu.getPcmbuf  (compID);
  PelBuf    recBuf  = cs.getRecoBuf ( area );
  CPelBuf   orgBuf  = cs.getOrgBuf  ( area );

  CHECK(pcmShiftRight < 0, "Negative shift");
  CompArea      tmpArea(COMPONENT_Y, area.chromaFormat, Position(0, 0), area.size());
  PelBuf tempOrgBuf = m_tmpStorageLCU.getBuf(tmpArea);
  tempOrgBuf.copyFrom(orgBuf);
  if (cs.slice->getLmcsEnabledFlag() && m_pcReshape->getCTUFlag() && compID == COMPONENT_Y)
  {
    tempOrgBuf.rspSignal(m_pcReshape->getFwdLUT());
  }
  for (uint32_t uiY = 0; uiY < pcmBuf.height; uiY++)
  {
    for (uint32_t uiX = 0; uiX < pcmBuf.width; uiX++)
    {
      // Encode
      pcmBuf.at(uiX, uiY) = tempOrgBuf.at(uiX, uiY) >> pcmShiftRight;
      // Reconstruction
      recBuf.at(uiX, uiY) = pcmBuf.at(uiX, uiY) << pcmShiftRight;
    }
  }
}

// -------------------------------------------------------------------------------------------------------------------
// Intra search
// -------------------------------------------------------------------------------------------------------------------

void IntraSearch::xEncIntraHeader( CodingStructure &cs, Partitioner &partitioner, const bool &bLuma, const bool &bChroma, const int subTuIdx )
{
  CodingUnit &cu = *cs.getCU( partitioner.chType );

  if (bLuma)
  {
    bool isFirst = cu.ispMode ? subTuIdx == 0 : partitioner.currArea().lumaPos() == cs.area.lumaPos();

    // CU header
    if( isFirst )
    {
      if ((!cs.slice->isIntra() || cs.slice->getSPS()->getIBCFlag())
        && cu.Y().valid()
        )
      {
        if( cs.pps->getTransquantBypassEnabledFlag() )
        {
          m_CABACEstimator->cu_transquant_bypass_flag( cu );
        }
        m_CABACEstimator->cu_skip_flag( cu );
        m_CABACEstimator->pred_mode   ( cu );
      }
      m_CABACEstimator->bdpcm_mode  ( cu, ComponentID(partitioner.chType) );
      if( CU::isIntra(cu) )
      {
        m_CABACEstimator->pcm_data( cu, partitioner );
        if( cu.ipcm )
        {
          return;
        }
      }
    }

    PredictionUnit &pu = *cs.getPU(partitioner.currArea().lumaPos(), partitioner.chType);

    // luma prediction mode
    if (isFirst)
    {
      if ( !cu.Y().valid())
        m_CABACEstimator->pred_mode( cu );
      m_CABACEstimator->intra_luma_pred_mode( pu );
    }
  }

  if (bChroma)
  {
    bool isFirst = partitioner.currArea().Cb().valid() && partitioner.currArea().chromaPos() == cs.area.chromaPos();

    PredictionUnit &pu = *cs.getPU( partitioner.currArea().chromaPos(), CHANNEL_TYPE_CHROMA );

    if( isFirst )
    {
      m_CABACEstimator->intra_chroma_pred_mode( pu );
    }
  }
}

void IntraSearch::xEncSubdivCbfQT( CodingStructure &cs, Partitioner &partitioner, const bool &bLuma, const bool &bChroma, const int subTuIdx, const PartSplit ispType )
{
  const UnitArea &currArea = partitioner.currArea();
          int subTuCounter = subTuIdx;
  TransformUnit &currTU = *cs.getTU( currArea.blocks[partitioner.chType], partitioner.chType, subTuCounter );
  CodingUnit    &currCU = *currTU.cu;
  uint32_t currDepth           = partitioner.currTrDepth;

  const bool subdiv        = currTU.depth > currDepth;
  ComponentID compID = partitioner.chType == CHANNEL_TYPE_LUMA ? COMPONENT_Y : COMPONENT_Cb;
  const bool chromaCbfISP = currArea.blocks[COMPONENT_Cb].valid() && currCU.ispMode && !subdiv;

  if( partitioner.canSplit( TU_MAX_TR_SPLIT, cs ) )
  {
    CHECK( !subdiv, "TU split implied" );
  }
  else
  {
    CHECK( subdiv && !currCU.ispMode && isLuma( compID ), "No TU subdivision is allowed with QTBT" );
  }

  if( bChroma && ( !currCU.ispMode || chromaCbfISP ) )
  {
    const uint32_t numberValidComponents = getNumberValidComponents(currArea.chromaFormat);
    const uint32_t cbfDepth = ( chromaCbfISP ? currDepth - 1 : currDepth );

    for (uint32_t ch = COMPONENT_Cb; ch < numberValidComponents; ch++)
    {
      const ComponentID compID = ComponentID(ch);

      if( currDepth == 0 || TU::getCbfAtDepth( currTU, compID, currDepth - 1 ) || chromaCbfISP )
      {
        const bool prevCbf = ( compID == COMPONENT_Cr ? TU::getCbfAtDepth( currTU, COMPONENT_Cb, currDepth ) : false );
        m_CABACEstimator->cbf_comp( cs, TU::getCbfAtDepth( currTU, compID, currDepth ), currArea.blocks[compID], cbfDepth, prevCbf );

      }
    }
  }

  if (subdiv)
  {

    if( partitioner.canSplit( TU_MAX_TR_SPLIT, cs ) )
    {
      partitioner.splitCurrArea( TU_MAX_TR_SPLIT, cs );
    }
    else if( currCU.ispMode && isLuma( compID ) )
    {
      partitioner.splitCurrArea( ispType, cs );
    }
    else
    THROW( "Cannot perform an implicit split!" );

    do
    {
      xEncSubdivCbfQT( cs, partitioner, bLuma, bChroma, subTuCounter, ispType );
      subTuCounter += subTuCounter != -1 ? 1 : 0;
    } while( partitioner.nextPart( cs ) );

    partitioner.exitCurrSplit();
  }
  else
  {
    //===== Cbfs =====
    if (bLuma)
    {
      bool previousCbf       = false;
      bool lastCbfIsInferred = false;
      if( ispType != TU_NO_ISP )
      {
        bool rootCbfSoFar = false;
        uint32_t nTus = currCU.ispMode == HOR_INTRA_SUBPARTITIONS ? currCU.lheight() >> g_aucLog2[currTU.lheight()] : currCU.lwidth() >> g_aucLog2[currTU.lwidth()];
        if( subTuCounter == nTus - 1 )
        {
          TransformUnit* tuPointer = currCU.firstTU;
          for( int tuIdx = 0; tuIdx < nTus - 1; tuIdx++ )
          {
            rootCbfSoFar |= TU::getCbfAtDepth( *tuPointer, COMPONENT_Y, currDepth );
            tuPointer = tuPointer->next;
          }
          if( !rootCbfSoFar )
          {
            lastCbfIsInferred = true;
          }
        }
        if( !lastCbfIsInferred )
        {
          previousCbf = TU::getPrevTuCbfAtDepth( currTU, COMPONENT_Y, partitioner.currTrDepth );
        }
      }
      if( !lastCbfIsInferred )
      {
        m_CABACEstimator->cbf_comp( cs, TU::getCbfAtDepth( currTU, COMPONENT_Y, currDepth ), currTU.Y(), currTU.depth, previousCbf, currCU.ispMode );
      }
    }
  }
}

void IntraSearch::xEncCoeffQT( CodingStructure &cs, Partitioner &partitioner, const ComponentID compID, const int subTuIdx, const PartSplit ispType )
{
  const UnitArea &currArea  = partitioner.currArea();

       int subTuCounter     = subTuIdx;
  TransformUnit &currTU     = *cs.getTU( currArea.blocks[partitioner.chType], partitioner.chType, subTuIdx );
  uint32_t      currDepth       = partitioner.currTrDepth;
  const bool subdiv         = currTU.depth > currDepth;

  if (subdiv)
  {
    if (partitioner.canSplit(TU_MAX_TR_SPLIT, cs))
    {
      partitioner.splitCurrArea(TU_MAX_TR_SPLIT, cs);
    }
    else if( currTU.cu->ispMode )
    {
      partitioner.splitCurrArea( ispType, cs );
    }
    else
      THROW("Implicit TU split not available!");

    do
    {
      xEncCoeffQT( cs, partitioner, compID, subTuCounter, ispType );
      subTuCounter += subTuCounter != -1 ? 1 : 0;
    } while( partitioner.nextPart( cs ) );

    partitioner.exitCurrSplit();
  }
  else

  if( currArea.blocks[compID].valid() )
  {
#if JVET_O0105_ICT
    if( compID == COMPONENT_Cr )
    {
      const int cbfMask = ( TU::getCbf( currTU, COMPONENT_Cb ) ? 2 : 0 ) + ( TU::getCbf( currTU, COMPONENT_Cr ) ? 1 : 0 );
      m_CABACEstimator->joint_cb_cr( currTU, cbfMask );
    }
#endif
    if( TU::hasCrossCompPredInfo( currTU, compID ) )
    {
      m_CABACEstimator->cross_comp_pred( currTU, compID );
    }
    if( TU::getCbf( currTU, compID ) )
    {
      m_CABACEstimator->residual_coding( currTU, compID );
    }
  }
}

uint64_t IntraSearch::xGetIntraFracBitsQT( CodingStructure &cs, Partitioner &partitioner, const bool &bLuma, const bool &bChroma, const int subTuIdx, const PartSplit ispType )
{
  m_CABACEstimator->resetBits();

  xEncIntraHeader( cs, partitioner, bLuma, bChroma, subTuIdx );
  xEncSubdivCbfQT( cs, partitioner, bLuma, bChroma, subTuIdx, ispType );


  if( bLuma )
  {
    xEncCoeffQT( cs, partitioner, COMPONENT_Y, subTuIdx, ispType );
  }
  if( bChroma )
  {
    xEncCoeffQT( cs, partitioner, COMPONENT_Cb, subTuIdx, ispType );
    xEncCoeffQT( cs, partitioner, COMPONENT_Cr, subTuIdx, ispType );
  }

  uint64_t fracBits = m_CABACEstimator->getEstFracBits();
  return fracBits;
}

uint64_t IntraSearch::xGetIntraFracBitsQTSingleChromaComponent( CodingStructure &cs, Partitioner &partitioner, const ComponentID compID )
{
  m_CABACEstimator->resetBits();

  if( compID == COMPONENT_Cb )
  {
    //intra mode coding
    PredictionUnit &pu = *cs.getPU( partitioner.currArea().lumaPos(), partitioner.chType );
    m_CABACEstimator->intra_chroma_pred_mode( pu );
    //xEncIntraHeader(cs, partitioner, false, true);
  }
  CHECK( partitioner.currTrDepth != 1, "error in the depth!" );
  const UnitArea &currArea = partitioner.currArea();

  TransformUnit &currTU = *cs.getTU( currArea.blocks[partitioner.chType], partitioner.chType );

  //cbf coding
#if JVET_O0105_ICT
  const bool prevCbf = ( compID == COMPONENT_Cr ? TU::getCbfAtDepth( currTU, COMPONENT_Cb, partitioner.currTrDepth ) : false );
  m_CABACEstimator->cbf_comp( cs, TU::getCbfAtDepth( currTU, compID, partitioner.currTrDepth ), currArea.blocks[compID], partitioner.currTrDepth - 1, prevCbf );
#else
  m_CABACEstimator->cbf_comp( cs, TU::getCbfAtDepth( currTU, compID, partitioner.currTrDepth ), currArea.blocks[compID], partitioner.currTrDepth - 1 );
#endif
  //coeffs coding and cross comp coding
  if( TU::hasCrossCompPredInfo( currTU, compID ) )
  {
    m_CABACEstimator->cross_comp_pred( currTU, compID );
  }
  if( TU::getCbf( currTU, compID ) )
  {
    m_CABACEstimator->residual_coding( currTU, compID );
  }

  uint64_t fracBits = m_CABACEstimator->getEstFracBits();
  return fracBits;
}

uint64_t IntraSearch::xGetIntraFracBitsQTChroma(TransformUnit& currTU, const ComponentID &compID)
{
  m_CABACEstimator->resetBits();

  if( TU::hasCrossCompPredInfo( currTU, compID ) )
  {
    m_CABACEstimator->cross_comp_pred( currTU, compID );
  }

  // Include Cbf and jointCbCr flags here as we make decisions across components
  CodingStructure &cs = *currTU.cs;

  if ( currTU.jointCbCr )
  {
#if JVET_O0105_ICT
    const int cbfMask = ( TU::getCbf( currTU, COMPONENT_Cb ) ? 2 : 0 ) + ( TU::getCbf( currTU, COMPONENT_Cr ) ? 1 : 0 );
    m_CABACEstimator->cbf_comp( cs, cbfMask>>1, currTU.blocks[ COMPONENT_Cb ], currTU.depth, false );
    m_CABACEstimator->cbf_comp( cs, cbfMask &1, currTU.blocks[ COMPONENT_Cr ], currTU.depth, cbfMask>>1 );
    if( cbfMask )
      m_CABACEstimator->joint_cb_cr( currTU, cbfMask );
    if( cbfMask >> 1 )
      m_CABACEstimator->residual_coding( currTU, COMPONENT_Cb );
    if( cbfMask & 1 )
      m_CABACEstimator->residual_coding( currTU, COMPONENT_Cr );
#else
    if ( TU::getCbf( currTU, COMPONENT_Cb ) )
    {
      m_CABACEstimator->cbf_comp( cs, true, currTU.blocks[ COMPONENT_Cb ], currTU.depth, false );
      m_CABACEstimator->cbf_comp( cs, true, currTU.blocks[ COMPONENT_Cr ], currTU.depth, true );
      m_CABACEstimator->joint_cb_cr( currTU );
    }
    else
    {
      m_CABACEstimator->cbf_comp( cs, false, currTU.blocks[ COMPONENT_Cb ], currTU.depth, false );
      m_CABACEstimator->cbf_comp( cs, false, currTU.blocks[ COMPONENT_Cr ], currTU.depth, false );
    }
#endif
  }
  else
  {
    if ( compID == COMPONENT_Cb )
      m_CABACEstimator->cbf_comp( cs, TU::getCbf( currTU, compID ), currTU.blocks[ compID ], currTU.depth, false );
    else
#if JVET_O0105_ICT
    {
      const bool cbCbf    = TU::getCbf( currTU, COMPONENT_Cb );
      const bool crCbf    = TU::getCbf( currTU, compID );
      const int  cbfMask  = ( cbCbf ? 2 : 0 ) + ( crCbf ? 1 : 0 );
      m_CABACEstimator->cbf_comp( cs, crCbf, currTU.blocks[ compID ], currTU.depth, cbCbf );
      m_CABACEstimator->joint_cb_cr( currTU, cbfMask );
    }
#else
      m_CABACEstimator->cbf_comp( cs, TU::getCbf( currTU, compID ), currTU.blocks[ compID ], currTU.depth, TU::getCbf( currTU, COMPONENT_Cb ) );
#endif
  }

#if JVET_O0105_ICT
  if( !currTU.jointCbCr && TU::getCbf( currTU, compID ) )
#else
  if( TU::getCbf( currTU, compID ) )
#endif
  {
    m_CABACEstimator->residual_coding( currTU, compID );
  }

  uint64_t fracBits = m_CABACEstimator->getEstFracBits();
  return fracBits;
}

void IntraSearch::xIntraCodingTUBlock(TransformUnit &tu, const ComponentID &compID, const bool &checkCrossCPrediction, Distortion& ruiDist, const int &default0Save1Load2, uint32_t* numSig, std::vector<TrMode>* trModes, const bool loadTr)
{
  if (!tu.blocks[compID].valid())
  {
    return;
  }

  CodingStructure &cs                       = *tu.cs;
  m_pcRdCost->setChromaFormat(cs.sps->getChromaFormatIdc());

  const CompArea      &area                 = tu.blocks[compID];
  const SPS           &sps                  = *cs.sps;
  const PPS           &pps                  = *cs.pps;

  const ChannelType    chType               = toChannelType(compID);
  const int            bitDepth             = sps.getBitDepth(chType);

  PelBuf         piOrg                      = cs.getOrgBuf    (area);
  PelBuf         piPred                     = cs.getPredBuf   (area);
  PelBuf         piResi                     = cs.getResiBuf   (area);
  PelBuf         piOrgResi                  = cs.getOrgResiBuf(area);
  PelBuf         piReco                     = cs.getRecoBuf   (area);

  const PredictionUnit &pu                  = *cs.getPU(area.pos(), chType);
  const uint32_t           uiChFinalMode        = PU::getFinalIntraMode(pu, chType);

  const bool           bUseCrossCPrediction = pps.getPpsRangeExtension().getCrossComponentPredictionEnabledFlag() && isChroma( compID ) && PU::isChromaIntraModeCrossCheckMode( pu ) && checkCrossCPrediction;
  const bool           ccUseRecoResi        = m_pcEncCfg->getUseReconBasedCrossCPredictionEstimate();
#if INCLUDE_ISP_CFG_FLAG
  const bool           ispSplitIsAllowed    = sps.getUseISP() && CU::canUseISPSplit( *tu.cu, compID );
#else
  const bool           ispSplitIsAllowed    = CU::canUseISPSplit( *tu.cu, compID );
#endif


  //===== init availability pattern =====
#if JVET_O0105_ICT
  CHECK( tu.jointCbCr && compID == COMPONENT_Cr, "wrong combination of compID and jointCbCr" );
#endif
  bool jointCbCr = tu.jointCbCr && compID == COMPONENT_Cb;

  if ( compID == COMPONENT_Y )
  {
  PelBuf sharedPredTS( m_pSharedPredTransformSkip[compID], area );
  if( default0Save1Load2 != 2 )
  {
    initIntraPatternChType( *tu.cu, area );

    //===== get prediction signal =====
    if( compID != COMPONENT_Y && PU::isLMCMode( uiChFinalMode ) )
    {
      {
        xGetLumaRecPixels( pu, area );
      }
      predIntraChromaLM( compID, piPred, pu, area, uiChFinalMode );
    }
    else
    {
      if( PU::isMIP( pu, chType ) )
      {
        predIntraMip( compID, piPred, pu );
      }
      else
      {
      predIntraAng( compID, piPred, pu );
      }
    }


    // save prediction
    if( default0Save1Load2 == 1 )
    {
      sharedPredTS.copyFrom( piPred );
    }
  }
  else
  {
    // load prediction
    piPred.copyFrom( sharedPredTS );
  }
  }


  DTRACE( g_trace_ctx, D_PRED, "@(%4d,%4d) [%2dx%2d] IMode=%d\n", tu.lx(), tu.ly(), tu.lwidth(), tu.lheight(), uiChFinalMode );
  //DTRACE_PEL_BUF( D_PRED, piPred, tu, tu.cu->predMode, COMPONENT_Y );

  const Slice           &slice = *cs.slice;
  bool flag = slice.getLmcsEnabledFlag() && (slice.isIntra() || (!slice.isIntra() && m_pcReshape->getCTUFlag()));
#if JVET_O0105_ICT
  if (isLuma(compID))
  {
#else
  if (flag && slice.getLmcsChromaResidualScaleFlag() && isChroma(compID))
  {
    const Area area = tu.Y().valid() ? tu.Y() : Area(recalcPosition(tu.chromaFormat, tu.chType, CHANNEL_TYPE_LUMA, tu.blocks[tu.chType].pos()), recalcSize(tu.chromaFormat, tu.chType, CHANNEL_TYPE_LUMA, tu.blocks[tu.chType].size()));
    const CompArea &areaY = CompArea(COMPONENT_Y, tu.chromaFormat, area );
#if JVET_O1109_UNFIY_CRS
    int adj = m_pcReshape->calculateChromaAdjVpduNei(tu, areaY);
#else
    PelBuf piPredY;
    piPredY = cs.picture->getPredBuf(areaY);
    const Pel avgLuma = piPredY.computeAvg();
    int adj = m_pcReshape->calculateChromaAdj(avgLuma);
#endif
    tu.setChromaAdj(adj);
  }
#endif
  //===== get residual signal =====
  piResi.copyFrom( piOrg  );
  if (slice.getLmcsEnabledFlag() && m_pcReshape->getCTUFlag() && compID == COMPONENT_Y)
  {
    CompArea      tmpArea(COMPONENT_Y, area.chromaFormat, Position(0, 0), area.size());
    PelBuf tmpPred = m_tmpStorageLCU.getBuf(tmpArea);
    tmpPred.copyFrom(piPred);
    piResi.rspSignal(m_pcReshape->getFwdLUT());
    piResi.subtract(tmpPred);
  }
  else
  piResi.subtract( piPred );

  if (pps.getPpsRangeExtension().getCrossComponentPredictionEnabledFlag() && isLuma(compID))
  {
    piOrgResi.copyFrom (piResi);
  }

  if (bUseCrossCPrediction)
  {
    if (xCalcCrossComponentPredictionAlpha(tu, compID, ccUseRecoResi) == 0)
    {
      return;
    }
    CrossComponentPrediction::crossComponentPrediction(tu, compID, cs.getResiBuf(tu.Y()), piResi, piResi, false);
  }
#if JVET_O0105_ICT
  }
#endif

  //===== transform and quantization =====
  //--- init rate estimation arrays for RDOQ ---
  //--- transform and quantization           ---
  TCoeff uiAbsSum = 0;

  const QpParam cQP(tu, compID);

#if RDOQ_CHROMA_LAMBDA
  m_pcTrQuant->selectLambda(compID);
#endif

  flag =flag && (tu.blocks[compID].width*tu.blocks[compID].height > 4);
  if (flag && isChroma(compID) && slice.getLmcsChromaResidualScaleFlag() )
  {
    int cResScaleInv = tu.getChromaAdj();
#if JVET_O0429_CRS_LAMBDA_FIX
    double cResScale = (double)(1 << CSCALE_FP_PREC) / (double)cResScaleInv;
#else
    double cResScale = round((double)(1 << CSCALE_FP_PREC) / (double)cResScaleInv);
#endif
    m_pcTrQuant->setLambda(m_pcTrQuant->getLambda() / (cResScale*cResScale));
#if !JVET_O0105_ICT
    if ( !jointCbCr ) // Joint CbCr signal is to be scaled in the case of joint chroma
    piResi.scaleSignal(cResScaleInv, 1, tu.cu->cs->slice->clpRng(compID));
#endif
  }

  const CompArea &crArea = tu.blocks     [ COMPONENT_Cr ];
  PelBuf          crOrg  = cs.getOrgBuf  ( crArea );
  PelBuf          crPred = cs.getPredBuf ( crArea );
  PelBuf          crResi = cs.getResiBuf ( crArea );
  PelBuf          crReco = cs.getRecoBuf ( crArea );

  if ( jointCbCr )
  {
#if !JVET_O0105_ICT
    // Get Cr prediction and residual
    crResi.copyFrom( crOrg  );
    crResi.subtract( crPred );

    // Create joint residual and store it for Cb component: jointResi = (cbResi - crResi)/2
    piResi.subtractAndHalve( crResi );

    // Scale the joint signal
    if ( flag && slice.getLmcsChromaResidualScaleFlag() )
      piResi.scaleSignal(tu.getChromaAdj(), 1, tu.cu->cs->slice->clpRng(compID));
#endif
    // Lambda is loosened for the joint mode with respect to single modes as the same residual is used for both chroma blocks
#if JVET_O0105_ICT
    const int    absIct = abs( TU::getICTMode(tu) );
    const double lfact  = ( absIct == 1 || absIct == 3 ? 0.8 : 0.5 );
    m_pcTrQuant->setLambda( lfact * m_pcTrQuant->getLambda() );
#else
    m_pcTrQuant->setLambda( 0.60 * m_pcTrQuant->getLambda() );
#endif
  }
#if JVET_O0105_ICT
  if( isChroma(compID) && tu.cu->cs->slice->getSliceQp() > 18 )
  {
    m_pcTrQuant->setLambda( 1.3 * m_pcTrQuant->getLambda() );
  }
#else
  else if ( isChroma(compID) && tu.cu->cs->slice->getSliceQp() > 18 )
    m_pcTrQuant->setLambda( 1.10 * m_pcTrQuant->getLambda() );
#endif

#if JVET_O0105_ICT
  if( isLuma(compID) )
  {
#endif
  double diagRatio = 0, horVerRatio = 0;

  if( trModes )
  {
    m_pcTrQuant->transformNxN( tu, compID, cQP, trModes, CU::isIntra( *tu.cu ) ? m_pcEncCfg->getIntraMTSMaxCand() : m_pcEncCfg->getInterMTSMaxCand(), ispSplitIsAllowed ? &diagRatio : nullptr, ispSplitIsAllowed ? &horVerRatio : nullptr );
    tu.mtsIdx = trModes->at(0).first;
  }
  m_pcTrQuant->transformNxN( tu, compID, cQP, uiAbsSum, m_CABACEstimator->getCtx(), loadTr, &diagRatio, &horVerRatio );
#if INCLUDE_ISP_CFG_FLAG
    if ( !tu.cu->ispMode && isLuma(compID) && ispSplitIsAllowed && tu.mtsIdx == MTS_DCT2_DCT2 && ispSplitIsAllowed )
#else
  if ( !tu.cu->ispMode && isLuma(compID) && ispSplitIsAllowed && tu.mtsIdx == MTS_DCT2_DCT2 )
#endif
  {
    m_intraModeDiagRatio        .push_back(diagRatio);
    m_intraModeHorVerRatio      .push_back(horVerRatio);
    m_intraModeTestedNormalIntra.push_back((int)uiChFinalMode);
  }


  DTRACE( g_trace_ctx, D_TU_ABS_SUM, "%d: comp=%d, abssum=%d\n", DTRACE_GET_COUNTER( g_trace_ctx, D_TU_ABS_SUM ), compID, uiAbsSum );


  //--- inverse transform ---
  if (uiAbsSum > 0)
  {
    m_pcTrQuant->invTransformNxN(tu, compID, piResi, cQP);
  }
  else
  {
    piResi.fill(0);
  }
#if JVET_O0105_ICT
  }
  else // chroma
  {
    int         codedCbfMask  = 0;
    ComponentID codeCompId    = ( tu.jointCbCr ? ( tu.jointCbCr>>1 ? COMPONENT_Cb : COMPONENT_Cr ) : compID );
    if( tu.jointCbCr )
    {
      ComponentID otherCompId = ( codeCompId==COMPONENT_Cr ? COMPONENT_Cb : COMPONENT_Cr );
      tu.getCoeffs( otherCompId ).fill(0); // do we need that?
      TU::setCbfAtDepth (tu, otherCompId, tu.depth, false );
    }
    PelBuf& codeResi = ( codeCompId == COMPONENT_Cr ? crResi : piResi );
    uiAbsSum = 0;
    m_pcTrQuant->transformNxN( tu, codeCompId, cQP, uiAbsSum, m_CABACEstimator->getCtx() );
    DTRACE( g_trace_ctx, D_TU_ABS_SUM, "%d: comp=%d, abssum=%d\n", DTRACE_GET_COUNTER( g_trace_ctx, D_TU_ABS_SUM ), codeCompId, uiAbsSum );
    if( uiAbsSum > 0 )
    {
      m_pcTrQuant->invTransformNxN(tu, codeCompId, codeResi, cQP);
      codedCbfMask += ( codeCompId == COMPONENT_Cb ? 2 : 1 );
    }
    else
    {
      codeResi.fill(0);
    }

    if( tu.jointCbCr )
    {
      if( tu.jointCbCr == 3 && codedCbfMask == 2 )
      {
        codedCbfMask = 3;
        TU::setCbfAtDepth (tu, COMPONENT_Cr, tu.depth, true );
      }
      if( tu.jointCbCr != codedCbfMask )
      {
        ruiDist = std::numeric_limits<Distortion>::max();
        return;
      }
      m_pcTrQuant->invTransformICT( tu, piResi, crResi );
      uiAbsSum = codedCbfMask;
    }
  }
#endif

  //===== reconstruction =====
  if ( flag && uiAbsSum > 0 && isChroma(compID) && slice.getLmcsChromaResidualScaleFlag() )
  {
    piResi.scaleSignal(tu.getChromaAdj(), 0, tu.cu->cs->slice->clpRng(compID));
#if JVET_O0105_ICT
    if( jointCbCr )
    {
      crResi.scaleSignal(tu.getChromaAdj(), 0, tu.cu->cs->slice->clpRng(COMPONENT_Cr));
    }
#endif
  }
  if (bUseCrossCPrediction)
  {
    CrossComponentPrediction::crossComponentPrediction(tu, compID, cs.getResiBuf(tu.Y()), piResi, piResi, true);
#if JVET_O0105_ICT
    if( jointCbCr )
    {
      CrossComponentPrediction::crossComponentPrediction(tu, COMPONENT_Cr, cs.getResiBuf(tu.Y()), crResi, crResi, true);
    }
#endif
  }

  if (slice.getLmcsEnabledFlag() && m_pcReshape->getCTUFlag() && compID == COMPONENT_Y)
  {
    CompArea      tmpArea(COMPONENT_Y, area.chromaFormat, Position(0,0), area.size());
    PelBuf tmpPred = m_tmpStorageLCU.getBuf(tmpArea);
    tmpPred.copyFrom(piPred);
    piReco.reconstruct(tmpPred, piResi, cs.slice->clpRng(compID));
  }
  else
#if JVET_O0105_ICT
  {
    piReco.reconstruct(piPred, piResi, cs.slice->clpRng( compID ));
    if( jointCbCr )
    {
      crReco.reconstruct(crPred, crResi, cs.slice->clpRng( COMPONENT_Cr ));
    }
  }
#else
  piReco.reconstruct(piPred, piResi, cs.slice->clpRng( compID ));
#endif

#if !JVET_O0105_ICT
  if ( jointCbCr )
  {
    // Cr uses negative of the signalled Cb residual
    if (uiAbsSum > 0)
      crResi.copyAndNegate( piResi );
    else
      crResi.fill(0);

    tu.getCoeffs(COMPONENT_Cr).fill(0);

    // Set cbf also for Cr
    TU::setCbfAtDepth (tu, COMPONENT_Cr, tu.depth, uiAbsSum > 0 ? true : false);

    // Cr reconstruction and its contribution to the total error
    crReco.reconstruct(crPred, crResi, cs.slice->clpRng( COMPONENT_Cr ));

#if WCG_EXT
    if ( m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled() ||
        (m_pcEncCfg->getReshaper()
          && slice.getLmcsEnabledFlag()
         && (m_pcReshape->getCTUFlag() || (isChroma(compID) && m_pcEncCfg->getReshapeIntraCMD()))))
    {
      const CPelBuf orgLuma = cs.getOrgBuf( cs.area.blocks[COMPONENT_Y] );
      ruiDist += m_pcRdCost->getDistPart( crOrg, crReco, bitDepth, COMPONENT_Cr, DF_SSE_WTD, &orgLuma );
    }
    else
#endif
    {
      ruiDist += m_pcRdCost->getDistPart( crOrg, crReco, bitDepth, COMPONENT_Cr, DF_SSE );
    }
  }
#endif

  //===== update distortion =====
#if WCG_EXT
  if (m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled() || (m_pcEncCfg->getReshaper()
    && slice.getLmcsEnabledFlag() && (m_pcReshape->getCTUFlag() || (isChroma(compID) && m_pcEncCfg->getReshapeIntraCMD()))))
  {
    const CPelBuf orgLuma = cs.getOrgBuf( cs.area.blocks[COMPONENT_Y] );
    if (compID == COMPONENT_Y  && !(m_pcEncCfg->getLumaLevelToDeltaQPMapping().isEnabled()))
    {
      CompArea      tmpArea1(COMPONENT_Y, area.chromaFormat, Position(0, 0), area.size());
      PelBuf tmpRecLuma = m_tmpStorageLCU.getBuf(tmpArea1);
      tmpRecLuma.copyFrom(piReco);
      tmpRecLuma.rspSignal(m_pcReshape->getInvLUT());
      ruiDist += m_pcRdCost->getDistPart(piOrg, tmpRecLuma, sps.getBitDepth(toChannelType(compID)), compID, DF_SSE_WTD, &orgLuma);
    }
    else
#if JVET_O0105_ICT
    {
      ruiDist += m_pcRdCost->getDistPart(piOrg, piReco, bitDepth, compID, DF_SSE_WTD, &orgLuma);
      if( jointCbCr )
      {
        ruiDist += m_pcRdCost->getDistPart(crOrg, crReco, bitDepth, COMPONENT_Cr, DF_SSE_WTD, &orgLuma);
      }
    }
#else
      ruiDist += m_pcRdCost->getDistPart(piOrg, piReco, bitDepth, compID, DF_SSE_WTD, &orgLuma);
#endif
  }
  else
#endif
  {
    ruiDist += m_pcRdCost->getDistPart( piOrg, piReco, bitDepth, compID, DF_SSE );
#if JVET_O0105_ICT
    if( jointCbCr )
    {
      ruiDist += m_pcRdCost->getDistPart( crOrg, crReco, bitDepth, COMPONENT_Cr, DF_SSE );
    }
#endif
  }
}

bool IntraSearch::xRecurIntraCodingLumaQT( CodingStructure &cs, Partitioner &partitioner, const double bestCostSoFar, const int subTuIdx, const PartSplit ispType, const bool ispIsCurrentWinner, bool mtsCheckRangeFlag, int mtsFirstCheckId, int mtsLastCheckId, bool moreProbMTSIdxFirst )
{
        int   subTuCounter = subTuIdx;
  const UnitArea &currArea = partitioner.currArea();
  const CodingUnit     &cu = *cs.getCU( currArea.lumaPos(), partitioner.chType );
        bool  earlySkipISP = false;
  uint32_t currDepth       = partitioner.currTrDepth;
  const SPS &sps           = *cs.sps;
  const PPS &pps           = *cs.pps;
  const bool keepResi      = pps.getPpsRangeExtension().getCrossComponentPredictionEnabledFlag() || KEEP_PRED_AND_RESI_SIGNALS;
  bool bCheckFull          = true;
  bool bCheckSplit         = false;
  bCheckFull               = !partitioner.canSplit( TU_MAX_TR_SPLIT, cs );
  bCheckSplit              = partitioner.canSplit( TU_MAX_TR_SPLIT, cs );

  if( cu.ispMode )
  {
    bCheckSplit = partitioner.canSplit( ispType, cs );
    bCheckFull = !bCheckSplit;
  }
  uint32_t    numSig           = 0;

  double     dSingleCost                        = MAX_DOUBLE;
  Distortion uiSingleDistLuma                   = 0;
  uint64_t   singleFracBits                     = 0;
  bool       checkTransformSkip                 = pps.getUseTransformSkip();
  int        bestModeId[ MAX_NUM_COMPONENT ]    = { 0, 0, 0 };
  uint8_t    nNumTransformCands                 = cu.mtsFlag ? 4 : 1;
  uint8_t    numTransformIndexCands             = nNumTransformCands;

  const TempCtx ctxStart  ( m_CtxCache, m_CABACEstimator->getCtx() );
  TempCtx       ctxBest   ( m_CtxCache );

  CodingStructure *csSplit = nullptr;
  CodingStructure *csFull  = nullptr;

  if( bCheckSplit )
  {
    csSplit = &cs;
  }
  else if( bCheckFull )
  {
    csFull = &cs;
  }

  bool validReturnFull = false;

  if( bCheckFull )
  {
    csFull->cost = 0.0;

    TransformUnit &tu = csFull->addTU( CS::getArea( *csFull, currArea, partitioner.chType ), partitioner.chType );
    tu.depth = currDepth;

    const bool tsAllowed  = TU::isTSAllowed( tu, COMPONENT_Y );
    const bool mtsAllowed = TU::isMTSAllowed( tu, COMPONENT_Y );
    std::vector<TrMode> trModes;

    if( sps.getUseLFNST() )
    {
      checkTransformSkip &= tsAllowed;
      checkTransformSkip &= !cu.mtsFlag;
      checkTransformSkip &= !cu.lfnstIdx;

      if( !cu.mtsFlag && checkTransformSkip )
      {
        trModes.push_back( TrMode( 0, true ) ); //DCT2
        trModes.push_back( TrMode( 1, true ) ); //TS
      }
    }
    else
    {
      nNumTransformCands = 1 + ( tsAllowed ? 1 : 0 ) + ( mtsAllowed ? 4 : 0 ); // DCT + TS + 4 MTS = 6 tests

      trModes.push_back( TrMode( 0, true ) ); //DCT2
      if( tsAllowed )
      {
        trModes.push_back( TrMode( 1, true ) );
      }
      if( mtsAllowed )
      {
        for( int i = 2; i < 6; i++ )
        {
          trModes.push_back( TrMode( i, true ) );
        }
      }
    }

    CHECK( !tu.Y().valid(), "Invalid TU" );

    CodingStructure &saveCS = *m_pSaveCS[0];

    TransformUnit *tmpTU = nullptr;

    Distortion singleDistTmpLuma = 0;
    uint64_t     singleTmpFracBits = 0;
    double     singleCostTmp     = 0;
    int        firstCheckId      = ( sps.getUseLFNST() && mtsCheckRangeFlag && cu.mtsFlag ) ? mtsFirstCheckId : 0;

    //we add the MTS candidates to the loop. TransformSkip will still be the last one to be checked (when modeId == lastCheckId) as long as checkTransformSkip is true
    int        lastCheckId       = sps.getUseLFNST() ? ( ( mtsCheckRangeFlag && cu.mtsFlag ) ? ( mtsLastCheckId + ( int ) checkTransformSkip ) : ( numTransformIndexCands - ( firstCheckId + 1 ) + ( int ) checkTransformSkip ) ) :
                                   trModes[ nNumTransformCands - 1 ].first;
    bool isNotOnlyOneMode        = sps.getUseLFNST() ? lastCheckId != firstCheckId : nNumTransformCands != 1;

    if( isNotOnlyOneMode )
    {
      saveCS.pcv     = cs.pcv;
      saveCS.picture = cs.picture;
      saveCS.area.repositionTo(cs.area);
      saveCS.clearTUs();
      tmpTU = &saveCS.addTU(currArea, partitioner.chType);
    }

    bool    cbfBestMode      = false;
    bool    cbfBestModeValid = false;
    bool    cbfDCT2  = true;

    double bestDCT2cost = MAX_DOUBLE;
    double threshold = m_pcEncCfg->getUseFastISP() && !cu.ispMode && ispIsCurrentWinner && nNumTransformCands > 1 ? 1 + 1.4 / sqrt( cu.lwidth() * cu.lheight() ) : 1;
    for( int modeId = firstCheckId; modeId <= ( sps.getUseLFNST() ? lastCheckId : ( nNumTransformCands - 1 ) ); modeId++ )
    {
      uint8_t transformIndex = modeId;

      if( sps.getUseLFNST() )
      {
        if( ( transformIndex < lastCheckId ) || ( ( transformIndex == lastCheckId ) && !checkTransformSkip ) ) //we avoid this if the mode is transformSkip
        {
          // Skip checking other transform candidates if zero CBF is encountered and it is the best transform so far
          if( m_pcEncCfg->getUseFastLFNST() && transformIndex && !cbfBestMode && cbfBestModeValid )
          {
            continue;
          }
        }
      }
      else
      {
        if( !cbfDCT2 || ( m_pcEncCfg->getUseTransformSkipFast() && bestModeId[ COMPONENT_Y ] == 1 ) )
        {
          break;
        }
        if( !trModes[ modeId ].second )
        {
          continue;
        }
        //we compare the DCT-II cost against the best ISP cost so far (except for TS)
        if( m_pcEncCfg->getUseFastISP() && !cu.ispMode && ispIsCurrentWinner && trModes[ modeId ].first != 0 && ( trModes[ modeId ].first != 1 || !tsAllowed ) && bestDCT2cost > bestCostSoFar * threshold )
        {
          continue;
        }
        tu.mtsIdx = trModes[ modeId ].first;
      }

      //we compare the best cost for non lwip
      const double thresholdSkipMode = 1.0 + (1.4 / sqrt((double)(cu.lwidth() * cu.lheight())));
      if ( cu.mipFlag && tu.mtsIdx && m_bestCostNonMip != MAX_DOUBLE && m_bestCostNonMip * thresholdSkipMode < bestDCT2cost  )
      {
        continue;
      }

      if ((modeId != firstCheckId) && isNotOnlyOneMode)
      {
        m_CABACEstimator->getCtx() = ctxStart;
      }

      int default0Save1Load2 = 0;
      singleDistTmpLuma = 0;

      if( modeId == firstCheckId && ( sps.getUseLFNST() ? ( modeId != lastCheckId ) : ( nNumTransformCands > 1 ) ) )
      {
        default0Save1Load2 = 1;
      }
      else if (modeId != firstCheckId)
      {
        if( sps.getUseLFNST() && !cbfBestModeValid )
        {
          default0Save1Load2 = 1;
        }
        else
        {
          default0Save1Load2 = 2;
        }
      }
      if( cu.ispMode )
      {
        default0Save1Load2 = 0;
      }
      if( sps.getUseLFNST() )
      {
        if( cu.mtsFlag )
        {
          if( moreProbMTSIdxFirst )
          {
            const ChannelType     chType      = toChannelType( COMPONENT_Y );
            const CompArea&       area        = tu.blocks[ COMPONENT_Y ];
            const PredictionUnit& pu          = *cs.getPU( area.pos(), chType );
            uint32_t              uiIntraMode = pu.intraDir[ chType ];

            if( transformIndex == 1 )
            {
              tu.mtsIdx = ( uiIntraMode < 34 ) ? MTS_DST7_DCT8 : MTS_DCT8_DST7;
            }
            else if( transformIndex == 2 )
            {
              tu.mtsIdx = ( uiIntraMode < 34 ) ? MTS_DCT8_DST7 : MTS_DST7_DCT8;
            }
            else
            {
              tu.mtsIdx = MTS_DST7_DST7 + transformIndex;
            }
          }
          else
          {
            tu.mtsIdx = MTS_DST7_DST7 + transformIndex;
          }
        }
        else
        {
          tu.mtsIdx = transformIndex;
        }

        if( !cu.mtsFlag && checkTransformSkip )
        {
          xIntraCodingTUBlock( tu, COMPONENT_Y, false, singleDistTmpLuma, default0Save1Load2, &numSig, modeId == 0 ? &trModes : nullptr, true );
          if( modeId == 0 )
          {
            for( int i = 0; i < 2; i++ )
            {
              if( trModes[ i ].second )
              {
                lastCheckId = trModes[ i ].first;
              }
            }
          }
        }
        else
        {
          xIntraCodingTUBlock( tu, COMPONENT_Y, false, singleDistTmpLuma, default0Save1Load2, &numSig );
        }
      }
      else
      {
        if( nNumTransformCands > 1 )
        {
          xIntraCodingTUBlock( tu, COMPONENT_Y, false, singleDistTmpLuma, default0Save1Load2, &numSig, modeId == 0 ? &trModes : nullptr, true );
          if( modeId == 0 )
          {
            for( int i = 0; i < nNumTransformCands; i++ )
            {
              if( trModes[ i ].second )
              {
                lastCheckId = trModes[ i ].first;
              }
            }
          }
        }
        else
        {
          xIntraCodingTUBlock( tu, COMPONENT_Y, false, singleDistTmpLuma, default0Save1Load2, &numSig );
        }
      }

      //----- determine rate and r-d cost -----
      if( ( sps.getUseLFNST() ? ( modeId == lastCheckId && modeId != 0 && checkTransformSkip ) : ( trModes[ modeId ].first != 0 ) ) && !TU::getCbfAtDepth( tu, COMPONENT_Y, currDepth ) )
      {
        //In order not to code TS flag when cbf is zero, the case for TS with cbf being zero is forbidden.
        singleCostTmp = MAX_DOUBLE;
      }
      else
      {
        if( cu.ispMode && m_pcRdCost->calcRdCost( csFull->fracBits, csFull->dist + singleDistTmpLuma ) > bestCostSoFar )
        {
          earlySkipISP = true;
        }
        else
        {
          singleTmpFracBits = xGetIntraFracBitsQT( *csFull, partitioner, true, false, subTuCounter, ispType );
        }
        singleCostTmp     = m_pcRdCost->calcRdCost( singleTmpFracBits, singleDistTmpLuma );
      }

      if ( !cu.ispMode && nNumTransformCands > 1 && modeId == firstCheckId )
      {
        bestDCT2cost = singleCostTmp;
      }
      if (!cu.ispMode && !cu.mipFlag && tu.mtsIdx == MTS_DCT2_DCT2 )
      {
        m_bestCostNonMip = std::min(m_bestCostNonMip, singleCostTmp);
      }

      if (singleCostTmp < dSingleCost)
      {
        dSingleCost       = singleCostTmp;
        uiSingleDistLuma  = singleDistTmpLuma;
        singleFracBits    = singleTmpFracBits;

        if( sps.getUseLFNST() )
        {
          bestModeId[ COMPONENT_Y ] = modeId;
          cbfBestMode = TU::getCbfAtDepth( tu, COMPONENT_Y, currDepth );
          cbfBestModeValid = true;
          validReturnFull = true;
        }
        else
        {
          bestModeId[ COMPONENT_Y ] = trModes[ modeId ].first;
          if( trModes[ modeId ].first == 0 )
          {
            cbfDCT2 = TU::getCbfAtDepth( tu, COMPONENT_Y, currDepth );
          }
        }

        if( bestModeId[COMPONENT_Y] != lastCheckId )
        {
          saveCS.getPredBuf( tu.Y() ).copyFrom( csFull->getPredBuf( tu.Y() ) );
          saveCS.getRecoBuf( tu.Y() ).copyFrom( csFull->getRecoBuf( tu.Y() ) );

          if( keepResi )
          {
            saveCS.getResiBuf   ( tu.Y() ).copyFrom( csFull->getResiBuf   ( tu.Y() ) );
            saveCS.getOrgResiBuf( tu.Y() ).copyFrom( csFull->getOrgResiBuf( tu.Y() ) );
          }

          tmpTU->copyComponentFrom( tu, COMPONENT_Y );

          ctxBest = m_CABACEstimator->getCtx();
        }
      }
    }

    if( sps.getUseLFNST() && !validReturnFull )
    {
      csFull->cost = MAX_DOUBLE;

      if( bCheckSplit )
      {
        ctxBest = m_CABACEstimator->getCtx();
      }
    }
    else
    {
      if( bestModeId[COMPONENT_Y] != lastCheckId )
      {
        csFull->getPredBuf( tu.Y() ).copyFrom( saveCS.getPredBuf( tu.Y() ) );
        csFull->getRecoBuf( tu.Y() ).copyFrom( saveCS.getRecoBuf( tu.Y() ) );

        if( keepResi )
        {
          csFull->getResiBuf   ( tu.Y() ).copyFrom( saveCS.getResiBuf   ( tu.Y() ) );
          csFull->getOrgResiBuf( tu.Y() ).copyFrom( saveCS.getOrgResiBuf( tu.Y() ) );
        }

        tu.copyComponentFrom( *tmpTU, COMPONENT_Y );

        if( !bCheckSplit )
        {
          m_CABACEstimator->getCtx() = ctxBest;
        }
      }
      else if( bCheckSplit )
      {
        ctxBest = m_CABACEstimator->getCtx();
      }

      csFull->cost     += dSingleCost;
      csFull->dist     += uiSingleDistLuma;
      csFull->fracBits += singleFracBits;
    }
  }

  bool validReturnSplit = false;
  if( bCheckSplit )
  {
    //----- store full entropy coding status, load original entropy coding status -----
    if( bCheckFull )
    {
      m_CABACEstimator->getCtx() = ctxStart;
    }
    //----- code splitted block -----
    csSplit->cost = 0;

    bool uiSplitCbfLuma  = false;
    bool splitIsSelected = true;
    if( partitioner.canSplit( TU_MAX_TR_SPLIT, cs ) )
    {
      partitioner.splitCurrArea( TU_MAX_TR_SPLIT, cs );
    }

    if( cu.ispMode )
    {
      partitioner.splitCurrArea( ispType, *csSplit );
    }
    do
    {
      bool tmpValidReturnSplit = xRecurIntraCodingLumaQT( *csSplit, partitioner, bestCostSoFar, subTuCounter, ispType, false, mtsCheckRangeFlag, mtsFirstCheckId, mtsLastCheckId );
      subTuCounter += subTuCounter != -1 ? 1 : 0;
      if( sps.getUseLFNST() && !tmpValidReturnSplit )
      {
        splitIsSelected = false;
        break;
      }

      if( !cu.ispMode )
      {
        csSplit->setDecomp( partitioner.currArea().Y() );
      }
      else if( CU::isISPFirst( cu, partitioner.currArea().Y(), COMPONENT_Y ) )
      {
        csSplit->setDecomp( cu.Y() );
      }

      uiSplitCbfLuma |= TU::getCbfAtDepth( *csSplit->getTU( partitioner.currArea().lumaPos(), partitioner.chType, subTuCounter - 1 ), COMPONENT_Y, partitioner.currTrDepth );
      if( cu.ispMode )
      {
        //exit condition if the accumulated cost is already larger than the best cost so far (no impact in RD performance)
        if( csSplit->cost > bestCostSoFar )
        {
          earlySkipISP    = true;
          splitIsSelected = false;
          break;
        }
        else
        {
          //more restrictive exit condition
          bool tuIsDividedInRows = CU::divideTuInRows( cu );
          int nSubPartitions = tuIsDividedInRows ? cu.lheight() >> g_aucLog2[cu.firstTU->lheight()] : cu.lwidth() >> g_aucLog2[cu.firstTU->lwidth()];
          double threshold = nSubPartitions == 2 ? 0.95 : subTuCounter == 1 ? 0.83 : 0.91;
          if( subTuCounter < nSubPartitions && csSplit->cost > bestCostSoFar*threshold )
          {
            earlySkipISP    = true;
            splitIsSelected = false;
            break;
          }
        }
      }



    } while( partitioner.nextPart( *csSplit ) );

    partitioner.exitCurrSplit();

    if( splitIsSelected )
    {
      for( auto &ptu : csSplit->tus )
      {
        if( currArea.Y().contains( ptu->Y() ) )
        {
          TU::setCbfAtDepth( *ptu, COMPONENT_Y, currDepth, uiSplitCbfLuma ? 1 : 0 );
        }
      }

      //----- restore context states -----
      m_CABACEstimator->getCtx() = ctxStart;

      //----- determine rate and r-d cost -----
      csSplit->fracBits = xGetIntraFracBitsQT( *csSplit, partitioner, true, false, cu.ispMode ? 0 : -1, ispType );

      //--- update cost ---
      csSplit->cost     = m_pcRdCost->calcRdCost(csSplit->fracBits, csSplit->dist);

      validReturnSplit = true;
    }
  }

  bool retVal = false;
  if( csFull || csSplit )
  {
    if( !sps.getUseLFNST() || validReturnFull || validReturnSplit )
    {
      {
        // otherwise this would've happened in useSubStructure
        cs.picture->getRecoBuf( currArea.Y() ).copyFrom( cs.getRecoBuf( currArea.Y() ) );
        cs.picture->getPredBuf( currArea.Y() ).copyFrom( cs.getPredBuf( currArea.Y() ) );
      }

      if( cu.ispMode && earlySkipISP )
      {
        cs.cost = MAX_DOUBLE;
      }
      else
      {
        cs.cost = m_pcRdCost->calcRdCost( cs.fracBits, cs.dist );
        retVal = true;
      }
    }
  }
  return retVal;
}

ChromaCbfs IntraSearch::xRecurIntraChromaCodingQT( CodingStructure &cs, Partitioner& partitioner, const double bestCostSoFar, const PartSplit ispType )
{
  UnitArea currArea                   = partitioner.currArea();
  const bool keepResi                 = cs.sps->getUseLMChroma() || KEEP_PRED_AND_RESI_SIGNALS;
  if( !currArea.Cb().valid() ) return ChromaCbfs( false );


  TransformUnit &currTU               = *cs.getTU( currArea.chromaPos(), CHANNEL_TYPE_CHROMA );
  const PredictionUnit &pu            = *cs.getPU( currArea.chromaPos(), CHANNEL_TYPE_CHROMA );

#if JVET_O0105_ICT
  bool lumaUsesISP                    = false;
#else
  bool lumaUsesISP                    = !CS::isDualITree( cs ) && currTU.cu->ispMode;
#endif
  uint32_t     currDepth                  = partitioner.currTrDepth;
  const PPS &pps                      = *cs.pps;
  ChromaCbfs cbfs                     ( false );

  if (currDepth == currTU.depth)
  {
    if (!currArea.Cb().valid() || !currArea.Cr().valid())
    {
      return cbfs;
    }


    CodingStructure &saveCS = *m_pSaveCS[1];
    saveCS.pcv      = cs.pcv;
    saveCS.picture  = cs.picture;
    saveCS.area.repositionTo( cs.area );
    saveCS.initStructData( MAX_INT, false, true );

    if( !CS::isDualITree( cs ) && currTU.cu->ispMode )
    {
      saveCS.clearCUs();
      CodingUnit& auxCU = saveCS.addCU( *currTU.cu, partitioner.chType );
      auxCU.ispMode = currTU.cu->ispMode;
      saveCS.sps = currTU.cs->sps;
      saveCS.clearPUs();
      saveCS.addPU( *currTU.cu->firstPU, partitioner.chType );
    }

    TransformUnit &tmpTU = saveCS.addTU(currArea, partitioner.chType);


    cs.setDecomp(currArea.Cb(), true); // set in advance (required for Cb2/Cr2 in 4:2:2 video)

    const unsigned      numTBlocks  = ::getNumberValidTBlocks( *cs.pcv );

    CompArea&  cbArea         = currTU.blocks[COMPONENT_Cb];
    CompArea&  crArea         = currTU.blocks[COMPONENT_Cr];
    double     bestCostCb     = MAX_DOUBLE;
    double     bestCostCr     = MAX_DOUBLE;
    Distortion bestDistCb     = 0;
    Distortion bestDistCr     = 0;
    int        maxModesTested = 0;
    bool       earlyExitISP   = false;

    TempCtx ctxStartTU( m_CtxCache );
    TempCtx ctxStart  ( m_CtxCache );
    TempCtx ctxBest   ( m_CtxCache );

    ctxStartTU       = m_CABACEstimator->getCtx();
    currTU.jointCbCr = 0;

    // Do predictions here to avoid repeating the "default0Save1Load2" stuff
    int  predMode   = PU::getFinalIntraMode( pu, CHANNEL_TYPE_CHROMA );

    PelBuf piPredCb = cs.getPredBuf(cbArea);
    PelBuf piPredCr = cs.getPredBuf(crArea);

    initIntraPatternChType( *currTU.cu, cbArea);
    initIntraPatternChType( *currTU.cu, crArea);

    if( PU::isLMCMode( predMode ) )
    {
      xGetLumaRecPixels( pu, cbArea );
      predIntraChromaLM( COMPONENT_Cb, piPredCb, pu, cbArea, predMode );
      predIntraChromaLM( COMPONENT_Cr, piPredCr, pu, crArea, predMode );
    }
    else
    {
      predIntraAng( COMPONENT_Cb, piPredCb, pu);
      predIntraAng( COMPONENT_Cr, piPredCr, pu);
    }

#if JVET_O0105_ICT
    // determination of chroma residuals including reshaping and cross-component prediction
    //----- get chroma residuals -----
    PelBuf resiCb  = cs.getResiBuf(cbArea);
    PelBuf resiCr  = cs.getResiBuf(crArea);
    resiCb.copyFrom( cs.getOrgBuf (cbArea) );
    resiCr.copyFrom( cs.getOrgBuf (crArea) );
    resiCb.subtract( piPredCb );
    resiCr.subtract( piPredCr );

    //----- get reshape parameter ----
    bool doReshaping = ( cs.slice->getLmcsEnabledFlag() && cs.slice->getLmcsChromaResidualScaleFlag()
                         && (cs.slice->isIntra() || m_pcReshape->getCTUFlag()) && (cbArea.width * cbArea.height > 4) );
    if( doReshaping )
    {
      const Area area = currTU.Y().valid() ? currTU.Y() : Area(recalcPosition(currTU.chromaFormat, currTU.chType, CHANNEL_TYPE_LUMA, currTU.blocks[currTU.chType].pos()), recalcSize(currTU.chromaFormat, currTU.chType, CHANNEL_TYPE_LUMA, currTU.blocks[currTU.chType].size()));
      const CompArea &areaY = CompArea(COMPONENT_Y, currTU.chromaFormat, area);
#if JVET_O1109_UNFIY_CRS
      int adj = m_pcReshape->calculateChromaAdjVpduNei(currTU, areaY);
#else
      PelBuf piPredY;
      piPredY = cs.picture->getPredBuf(areaY);
      const Pel avgLuma = piPredY.computeAvg();
      int adj = m_pcReshape->calculateChromaAdj(avgLuma);
#endif
      currTU.setChromaAdj(adj);
    }

    //----- get cross component prediction parameters -----
    bool checkCrossComponentPrediction = PU::isChromaIntraModeCrossCheckMode( pu ) && pps.getPpsRangeExtension().getCrossComponentPredictionEnabledFlag() && TU::getCbf( currTU, COMPONENT_Y );
    int  compAlpha[MAX_NUM_COMPONENT] = { 0, 0, 0 };
    if( checkCrossComponentPrediction )
    {
      compAlpha[COMPONENT_Cb] = xCalcCrossComponentPredictionAlpha( currTU, COMPONENT_Cb, m_pcEncCfg->getUseReconBasedCrossCPredictionEstimate() );
      compAlpha[COMPONENT_Cr] = xCalcCrossComponentPredictionAlpha( currTU, COMPONENT_Cr, m_pcEncCfg->getUseReconBasedCrossCPredictionEstimate() );
      if( compAlpha[COMPONENT_Cb] == 0 && compAlpha[COMPONENT_Cr] == 0 )
      {
        checkCrossComponentPrediction = false;
      }
    }

    //===== store original residual signals (std and crossCompPred) =====
    CompStorage  orgResiCb[5], orgResiCr[5]; // 0:std, 1-3:jointCbCr (placeholder at this stage), 4:crossComp
    for( int k = 0; k < (checkCrossComponentPrediction?5:1); k+=4 )
    {
      orgResiCb[k].create( cbArea );
      orgResiCr[k].create( crArea );
      if( k >= 4 ) {
        CrossComponentPrediction::crossComponentPrediction( currTU, COMPONENT_Cb, cs.getResiBuf(currTU.Y()), resiCb, orgResiCb[k], false);
        CrossComponentPrediction::crossComponentPrediction( currTU, COMPONENT_Cr, cs.getResiBuf(currTU.Y()), resiCr, orgResiCr[k], false);
      } else {
        orgResiCb[k].copyFrom( resiCb );
        orgResiCr[k].copyFrom( resiCr );
      }
      if( doReshaping )
      {
        int cResScaleInv = currTU.getChromaAdj();
        orgResiCb[k].scaleSignal( cResScaleInv, 1, currTU.cu->cs->slice->clpRng(COMPONENT_Cb) );
        orgResiCr[k].scaleSignal( cResScaleInv, 1, currTU.cu->cs->slice->clpRng(COMPONENT_Cr) );
      }
    }
#endif

    for( uint32_t c = COMPONENT_Cb; c < numTBlocks; c++)
    {
      const ComponentID compID  = ComponentID(c);
      const CompArea&   area    = currTU.blocks[compID];

      double     dSingleCost    = MAX_DOUBLE;
      int        bestModeId     = 0;
      Distortion singleDistCTmp = 0;
      double     singleCostTmp  = 0;

#if !JVET_O0105_ICT
      const bool checkCrossComponentPrediction = PU::isChromaIntraModeCrossCheckMode( pu ) && pps.getPpsRangeExtension().getCrossComponentPredictionEnabledFlag() && TU::getCbf( currTU, COMPONENT_Y );
#endif
      const int  crossCPredictionModesToTest = checkCrossComponentPrediction ? 2 : 1;
      const int  totalModesToTest            = crossCPredictionModesToTest;
      const bool isOneMode                   = false;
      maxModesTested                         = totalModesToTest > maxModesTested ? totalModesToTest : maxModesTested;

      int currModeId = 0;
      int default0Save1Load2 = 0;


      if (!isOneMode)
      {
        ctxStart = m_CABACEstimator->getCtx();
      }

      {
        for (int crossCPredictionModeId = 0; crossCPredictionModeId < crossCPredictionModesToTest; crossCPredictionModeId++)
        {
#if JVET_O0105_ICT
          resiCb.copyFrom( orgResiCb[4*crossCPredictionModeId] );
          resiCr.copyFrom( orgResiCr[4*crossCPredictionModeId] );

          currTU.compAlpha    [compID] = ( crossCPredictionModeId ? compAlpha[compID] : 0 );
#else
          currTU.compAlpha    [compID] = 0;
#endif

          currModeId++;

          const bool isFirstMode = (currModeId == 1);
          const bool isLastMode  = false; // Always store output to saveCS and tmpTU

          if (!isFirstMode) // if not first mode to be tested
          {
            m_CABACEstimator->getCtx() = ctxStart;
          }

          singleDistCTmp = 0;

          xIntraCodingTUBlock( currTU, compID, crossCPredictionModeId != 0, singleDistCTmp, default0Save1Load2 );

          if( ( ( crossCPredictionModeId == 1 ) && ( currTU.compAlpha[compID] == 0 ) ) ) //In order not to code TS flag when cbf is zero, the case for TS with cbf being zero is forbidden.
          {
            singleCostTmp = MAX_DOUBLE;
          }
          else if( lumaUsesISP && bestCostSoFar != MAX_DOUBLE && c == COMPONENT_Cb )
          {
            uint64_t fracBitsTmp = xGetIntraFracBitsQTSingleChromaComponent( cs, partitioner, ComponentID( c ) );
            singleCostTmp = m_pcRdCost->calcRdCost( fracBitsTmp, singleDistCTmp );
            if( isOneMode || ( !isOneMode && !isLastMode ) )
            {
              m_CABACEstimator->getCtx() = ctxStart;
            }
          }
          else if( !isOneMode )
          {
            uint64_t fracBitsTmp = xGetIntraFracBitsQTChroma( currTU, compID );
            singleCostTmp = m_pcRdCost->calcRdCost( fracBitsTmp, singleDistCTmp );
          }

          if( singleCostTmp < dSingleCost )
          {
            dSingleCost = singleCostTmp;
            bestModeId  = currModeId;

            if ( c == COMPONENT_Cb )
            {
              bestCostCb = singleCostTmp;
              bestDistCb = singleDistCTmp;
            }
            else
            {
              bestCostCr = singleCostTmp;
              bestDistCr = singleDistCTmp;
            }

            if( !isLastMode )
            {
#if KEEP_PRED_AND_RESI_SIGNALS
              saveCS.getPredBuf   (area).copyFrom(cs.getPredBuf   (area));
              saveCS.getOrgResiBuf(area).copyFrom(cs.getOrgResiBuf(area));
#endif
              saveCS.getPredBuf   (area).copyFrom(cs.getPredBuf   (area));
              if( keepResi )
              {
                saveCS.getResiBuf (area).copyFrom(cs.getResiBuf   (area));
              }
              saveCS.getRecoBuf   (area).copyFrom(cs.getRecoBuf   (area));

              tmpTU.copyComponentFrom(currTU, compID);

              ctxBest = m_CABACEstimator->getCtx();
            }
          }
        }
      }

      if( lumaUsesISP && dSingleCost > bestCostSoFar && c == COMPONENT_Cb )
      {
        //Luma + Cb cost is already larger than the best cost, so we don't need to test Cr
        cs.dist = MAX_UINT;
        m_CABACEstimator->getCtx() = ctxStart;
        earlyExitISP               = true;
        break;
        //return cbfs;
      }

      // Done with one component of separate coding of Cr and Cb, just switch to the best Cb contexts if Cr coding is still to be done
      if ( c == COMPONENT_Cb && bestModeId < totalModesToTest)
      {
        m_CABACEstimator->getCtx() = ctxBest;

        currTU.copyComponentFrom(tmpTU, COMPONENT_Cb); // Cbf of Cb is needed to estimate cost for Cr Cbf
      }
    }

    if ( !earlyExitISP )
    {
      // Test using joint chroma residual coding
      double     bestCostCbCr   = bestCostCb + bestCostCr;
      Distortion bestDistCbCr   = bestDistCb + bestDistCr;
      int        bestJointCbCr  = 0;
#if JVET_O0105_ICT
      bool       lastIsBest     = false;
      std::vector<int>  jointCbfMasksToTest;
      if( TU::getCbf(tmpTU, COMPONENT_Cb) || TU::getCbf(tmpTU, COMPONENT_Cr) )
      {
        jointCbfMasksToTest = m_pcTrQuant->selectICTCandidates( currTU, orgResiCb, orgResiCr );
      }

      for( int cbfMask : jointCbfMasksToTest )
#else
      bool       checkJointCbCr = TU::getCbf(tmpTU, COMPONENT_Cb) || TU::getCbf(tmpTU, COMPONENT_Cr);

      if ( checkJointCbCr )
#endif
      {
        Distortion distTmp = 0;

#if JVET_O0105_ICT
        currTU.jointCbCr               = (uint8_t)cbfMask;
#else
        currTU.jointCbCr               = 1;
#endif
        currTU.compAlpha[COMPONENT_Cb] = 0;
        currTU.compAlpha[COMPONENT_Cr] = 0;

        m_CABACEstimator->getCtx() = ctxStartTU;

#if JVET_O0105_ICT
        resiCb.copyFrom( orgResiCb[cbfMask] );
        resiCr.copyFrom( orgResiCr[cbfMask] );
        xIntraCodingTUBlock( currTU, COMPONENT_Cb, false, distTmp, 0 );

        double costTmp = std::numeric_limits<double>::max();
        if( distTmp < std::numeric_limits<Distortion>::max() )
        {
          uint64_t bits  = xGetIntraFracBitsQTChroma( currTU, COMPONENT_Cb );
          costTmp = m_pcRdCost->calcRdCost( bits, distTmp );
        }
#else
        xIntraCodingTUBlock( currTU, COMPONENT_Cb, false, distTmp, 0 );

        uint64_t bits  = xGetIntraFracBitsQTChroma( currTU, COMPONENT_Cb );
        double costTmp = m_pcRdCost->calcRdCost( bits, distTmp );
#endif

        if( costTmp < bestCostCbCr )
        {
          bestCostCbCr  = costTmp;
          bestDistCbCr  = distTmp;
#if JVET_O0105_ICT
          bestJointCbCr = currTU.jointCbCr;

          // store data
          if( cbfMask != jointCbfMasksToTest.back() )
          {
#if KEEP_PRED_AND_RESI_SIGNALS
            saveCS.getOrgResiBuf(cbArea).copyFrom(cs.getOrgResiBuf(cbArea));
            saveCS.getOrgResiBuf(crArea).copyFrom(cs.getOrgResiBuf(crArea));
#endif
            saveCS.getPredBuf   (cbArea).copyFrom(cs.getPredBuf   (cbArea));
            saveCS.getPredBuf   (crArea).copyFrom(cs.getPredBuf   (crArea));
            if( keepResi )
            {
              saveCS.getResiBuf (cbArea).copyFrom(cs.getResiBuf   (cbArea));
              saveCS.getResiBuf (crArea).copyFrom(cs.getResiBuf   (crArea));
            }
            saveCS.getRecoBuf   (cbArea).copyFrom(cs.getRecoBuf   (cbArea));
            saveCS.getRecoBuf   (crArea).copyFrom(cs.getRecoBuf   (crArea));

            tmpTU.copyComponentFrom(currTU, COMPONENT_Cb);
            tmpTU.copyComponentFrom(currTU, COMPONENT_Cr);

            ctxBest = m_CABACEstimator->getCtx();
          }
          else
          {
            lastIsBest = true;
          }
#else
          bestJointCbCr = 1;
#endif
        }
      }

      // Retrieve the best CU data (unless it was the very last one tested)
#if JVET_O0105_ICT
      if ( !( maxModesTested == 1 && jointCbfMasksToTest.empty() ) && !lastIsBest )
#else
      if ( !(maxModesTested == 1 && !checkJointCbCr) && bestJointCbCr == 0 )
#endif
      {
#if KEEP_PRED_AND_RESI_SIGNALS
        cs.getPredBuf   (cbArea).copyFrom(saveCS.getPredBuf   (cbArea));
        cs.getOrgResiBuf(cbArea).copyFrom(saveCS.getOrgResiBuf(cbArea));
        cs.getPredBuf   (crArea).copyFrom(saveCS.getPredBuf   (crArea));
        cs.getOrgResiBuf(crArea).copyFrom(saveCS.getOrgResiBuf(crArea));
#endif
        cs.getPredBuf   (cbArea).copyFrom(saveCS.getPredBuf   (cbArea));
        cs.getPredBuf   (crArea).copyFrom(saveCS.getPredBuf   (crArea));

        if( keepResi )
        {
          cs.getResiBuf (cbArea).copyFrom(saveCS.getResiBuf   (cbArea));
          cs.getResiBuf (crArea).copyFrom(saveCS.getResiBuf   (crArea));
        }
        cs.getRecoBuf   (cbArea).copyFrom(saveCS.getRecoBuf   (cbArea));
        cs.getRecoBuf   (crArea).copyFrom(saveCS.getRecoBuf   (crArea));

        currTU.copyComponentFrom(tmpTU, COMPONENT_Cb);
        currTU.copyComponentFrom(tmpTU, COMPONENT_Cr);

        m_CABACEstimator->getCtx() = ctxBest;
      }

      // Copy results to the picture structures
      cs.picture->getRecoBuf(cbArea).copyFrom(cs.getRecoBuf(cbArea));
      cs.picture->getRecoBuf(crArea).copyFrom(cs.getRecoBuf(crArea));
      cs.picture->getPredBuf(cbArea).copyFrom(cs.getPredBuf(cbArea));
      cs.picture->getPredBuf(crArea).copyFrom(cs.getPredBuf(crArea));

      cbfs.cbf(COMPONENT_Cb) = TU::getCbf(currTU, COMPONENT_Cb);
      cbfs.cbf(COMPONENT_Cr) = TU::getCbf(currTU, COMPONENT_Cr);

#if JVET_O0105_ICT
      currTU.jointCbCr = ( (cbfs.cbf(COMPONENT_Cb) + cbfs.cbf(COMPONENT_Cr)) ? bestJointCbCr : 0 );
#else
      currTU.jointCbCr = cbfs.cbf(COMPONENT_Cb) ? bestJointCbCr : 0;
#endif
      cs.dist         += bestDistCbCr;
    }
  }
  else
  {
    unsigned    numValidTBlocks   = ::getNumberValidTBlocks( *cs.pcv );
    ChromaCbfs  SplitCbfs         ( false );

    if( partitioner.canSplit( TU_MAX_TR_SPLIT, cs ) )
    {
      partitioner.splitCurrArea( TU_MAX_TR_SPLIT, cs );
    }
    else if( currTU.cu->ispMode )
    {
      partitioner.splitCurrArea( ispType, cs );
    }
    else
      THROW( "Implicit TU split not available" );

    do
    {
      ChromaCbfs subCbfs = xRecurIntraChromaCodingQT( cs, partitioner, bestCostSoFar, ispType );

      for( uint32_t ch = COMPONENT_Cb; ch < numValidTBlocks; ch++ )
      {
        const ComponentID compID = ComponentID( ch );
        SplitCbfs.cbf( compID ) |= subCbfs.cbf( compID );
      }
    } while( partitioner.nextPart( cs ) );

    partitioner.exitCurrSplit();

    if( lumaUsesISP && cs.dist == MAX_UINT )
    {
      return cbfs;
    }
    {

      cbfs.Cb |= SplitCbfs.Cb;
      cbfs.Cr |= SplitCbfs.Cr;

      if( !lumaUsesISP )
      {
        for( auto &ptu : cs.tus )
        {
          if( currArea.Cb().contains( ptu->Cb() ) || ( !ptu->Cb().valid() && currArea.Y().contains( ptu->Y() ) ) )
          {
            TU::setCbfAtDepth( *ptu, COMPONENT_Cb, currDepth, SplitCbfs.Cb );
            TU::setCbfAtDepth( *ptu, COMPONENT_Cr, currDepth, SplitCbfs.Cr );
          }
        }
      }
    }
  }

  return cbfs;
}

uint64_t IntraSearch::xFracModeBitsIntra(PredictionUnit &pu, const uint32_t &uiMode, const ChannelType &chType)
{
  uint32_t orgMode = uiMode;

  if (!pu.mhIntraFlag)
  std::swap(orgMode, pu.intraDir[chType]);

  m_CABACEstimator->resetBits();

  if( isLuma( chType ) )
  {
    if (!pu.mhIntraFlag)
    {
      m_CABACEstimator->intra_luma_pred_mode(pu);
    }
  }
  else
  {
    m_CABACEstimator->intra_chroma_pred_mode( pu );
  }

  if ( !pu.mhIntraFlag )
  std::swap(orgMode, pu.intraDir[chType]);

  return m_CABACEstimator->getEstFracBits();
}



void IntraSearch::encPredIntraDPCM( const ComponentID &compID, PelBuf &pOrg, PelBuf &pDst, const uint32_t &uiDirMode )
{
  CHECK( pOrg.buf == 0, "Encoder DPCM called without original buffer" );

  const int srcStride = m_topRefLength + 1;
  CPelBuf   pSrc = CPelBuf(getPredictorPtr(compID), srcStride, m_leftRefLength + 1);

  // Sample Adaptive intra-Prediction (SAP)
  if( uiDirMode == HOR_IDX )
  {
    // left column filled with reference samples, remaining columns filled with pOrg data
    for( int y = 0; y < pDst.height; y++ )
    {
      pDst.at( 0, y ) = pSrc.at( 0, 1 + y );
    }
    CPelBuf orgRest  = pOrg.subBuf( 0, 0, pOrg.width - 1, pOrg.height );
    PelBuf  predRest = pDst.subBuf( 1, 0, pDst.width - 1, pDst.height );

    predRest.copyFrom( orgRest );
  }
  else // VER_IDX
  {
    // top row filled with reference samples, remaining rows filled with pOrg data
    for( int x = 0; x < pDst.width; x++ )
    {
      pDst.at( x, 0 ) = pSrc.at( 1 + x, 0 );
    }
    CPelBuf orgRest  = pOrg.subBuf( 0, 0, pOrg.width, pOrg.height - 1 );
    PelBuf  predRest = pDst.subBuf( 0, 1, pDst.width, pDst.height - 1 );

    predRest.copyFrom( orgRest );
  }
}

bool IntraSearch::useDPCMForFirstPassIntraEstimation( const PredictionUnit &pu, const uint32_t &uiDirMode )
{
  return CU::isRDPCMEnabled( *pu.cu ) && pu.cu->transQuantBypass && (uiDirMode == HOR_IDX || uiDirMode == VER_IDX);
}

template<typename T, size_t N>
void IntraSearch::reduceHadCandList(static_vector<T, N>& candModeList, static_vector<double, N>& candCostList, int& numModesForFullRD, const double thresholdHadCost, const double thresholdHadCostConv)
{
  CHECKD(candModeList.size() != numModesForFullRD, "Error: list size");
  CHECKD(candCostList.size() != numModesForFullRD, "Error: list size");
  const int maxCandPerType = numModesForFullRD >> 1;
  static_vector<ModeInfo, FAST_UDI_MAX_RDMODE_NUM> tempRdModeList;
  static_vector<double, FAST_UDI_MAX_RDMODE_NUM> tempCandCostList;
  const double minCost = candCostList[0];

  int numConv = 0;
  for (int idx = 0; idx < candModeList.size(); idx++)
  {
    ModeInfo uiOrgMode = candModeList[idx];

    if (!uiOrgMode.mipFlg) { numConv++; }

    if (uiOrgMode.mipFlg || (numConv <= maxCandPerType))
    {
      tempRdModeList.push_back(uiOrgMode);
      tempCandCostList.push_back(candCostList[idx]);
    }
    else if (candCostList[idx] < thresholdHadCostConv * minCost)
    {
      tempRdModeList.push_back(uiOrgMode);
      tempCandCostList.push_back(candCostList[idx]);
    }
  }
  candModeList = tempRdModeList;
  candCostList = tempCandCostList;

  int numMip = 0;
  tempRdModeList.clear();
  tempCandCostList.clear();
  for (int idx = 0; idx < candModeList.size(); idx++)
  {
    ModeInfo uiOrgMode = candModeList[idx];
    if (uiOrgMode.mipFlg)
    {
      numMip++;
    }
    if (!uiOrgMode.mipFlg || (numMip <= maxCandPerType))
    {
      tempRdModeList.push_back(uiOrgMode);
      tempCandCostList.push_back(candCostList[idx]);
    }
    else if (candCostList[idx] < thresholdHadCost * minCost)
    {
      tempRdModeList.push_back(uiOrgMode);
      tempCandCostList.push_back(candCostList[idx]);
    }
  }
  candModeList = tempRdModeList;
  candCostList = tempCandCostList;
  numModesForFullRD = int(candModeList.size());
}
