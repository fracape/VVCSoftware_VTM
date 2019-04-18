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

/** \file     Contexts.cpp
 *  \brief    Classes providing probability descriptions and contexts (also contains context initialization values)
 */

#include "Contexts.h"

#include <algorithm>
#include <cstring>
#include <limits>


const uint8_t ProbModelTables::m_RenormTable_32[32] =
{
  6,  5,  4,  4,
  3,  3,  3,  3,
  2,  2,  2,  2,
  2,  2,  2,  2,
  1,  1,  1,  1,
  1,  1,  1,  1,
  1,  1,  1,  1,
  1,  1,  1,  1
};

const BinFracBits ProbModelTables::m_binFracBits[256] = {
  { { 0x0005c, 0x48000 } }, { { 0x00116, 0x3b520 } }, { { 0x001d0, 0x356cb } }, { { 0x0028b, 0x318a9 } },
  { { 0x00346, 0x2ea40 } }, { { 0x00403, 0x2c531 } }, { { 0x004c0, 0x2a658 } }, { { 0x0057e, 0x28beb } },
  { { 0x0063c, 0x274ce } }, { { 0x006fc, 0x26044 } }, { { 0x007bc, 0x24dc9 } }, { { 0x0087d, 0x23cfc } },
  { { 0x0093f, 0x22d96 } }, { { 0x00a01, 0x21f60 } }, { { 0x00ac4, 0x2122e } }, { { 0x00b89, 0x205dd } },
  { { 0x00c4e, 0x1fa51 } }, { { 0x00d13, 0x1ef74 } }, { { 0x00dda, 0x1e531 } }, { { 0x00ea2, 0x1db78 } },
  { { 0x00f6a, 0x1d23c } }, { { 0x01033, 0x1c970 } }, { { 0x010fd, 0x1c10b } }, { { 0x011c8, 0x1b903 } },
  { { 0x01294, 0x1b151 } }, { { 0x01360, 0x1a9ee } }, { { 0x0142e, 0x1a2d4 } }, { { 0x014fc, 0x19bfc } },
  { { 0x015cc, 0x19564 } }, { { 0x0169c, 0x18f06 } }, { { 0x0176d, 0x188de } }, { { 0x0183f, 0x182e8 } },
  { { 0x01912, 0x17d23 } }, { { 0x019e6, 0x1778a } }, { { 0x01abb, 0x1721c } }, { { 0x01b91, 0x16cd5 } },
  { { 0x01c68, 0x167b4 } }, { { 0x01d40, 0x162b6 } }, { { 0x01e19, 0x15dda } }, { { 0x01ef3, 0x1591e } },
  { { 0x01fcd, 0x15480 } }, { { 0x020a9, 0x14fff } }, { { 0x02186, 0x14b99 } }, { { 0x02264, 0x1474e } },
  { { 0x02343, 0x1431b } }, { { 0x02423, 0x13f01 } }, { { 0x02504, 0x13afd } }, { { 0x025e6, 0x1370f } },
  { { 0x026ca, 0x13336 } }, { { 0x027ae, 0x12f71 } }, { { 0x02894, 0x12bc0 } }, { { 0x0297a, 0x12821 } },
  { { 0x02a62, 0x12494 } }, { { 0x02b4b, 0x12118 } }, { { 0x02c35, 0x11dac } }, { { 0x02d20, 0x11a51 } },
  { { 0x02e0c, 0x11704 } }, { { 0x02efa, 0x113c7 } }, { { 0x02fe9, 0x11098 } }, { { 0x030d9, 0x10d77 } },
  { { 0x031ca, 0x10a63 } }, { { 0x032bc, 0x1075c } }, { { 0x033b0, 0x10461 } }, { { 0x034a5, 0x10173 } },
  { { 0x0359b, 0x0fe90 } }, { { 0x03693, 0x0fbb9 } }, { { 0x0378c, 0x0f8ed } }, { { 0x03886, 0x0f62b } },
  { { 0x03981, 0x0f374 } }, { { 0x03a7e, 0x0f0c7 } }, { { 0x03b7c, 0x0ee23 } }, { { 0x03c7c, 0x0eb89 } },
  { { 0x03d7d, 0x0e8f9 } }, { { 0x03e7f, 0x0e671 } }, { { 0x03f83, 0x0e3f2 } }, { { 0x04088, 0x0e17c } },
  { { 0x0418e, 0x0df0e } }, { { 0x04297, 0x0dca8 } }, { { 0x043a0, 0x0da4a } }, { { 0x044ab, 0x0d7f3 } },
  { { 0x045b8, 0x0d5a5 } }, { { 0x046c6, 0x0d35d } }, { { 0x047d6, 0x0d11c } }, { { 0x048e7, 0x0cee3 } },
  { { 0x049fa, 0x0ccb0 } }, { { 0x04b0e, 0x0ca84 } }, { { 0x04c24, 0x0c85e } }, { { 0x04d3c, 0x0c63f } },
  { { 0x04e55, 0x0c426 } }, { { 0x04f71, 0x0c212 } }, { { 0x0508d, 0x0c005 } }, { { 0x051ac, 0x0bdfe } },
  { { 0x052cc, 0x0bbfc } }, { { 0x053ee, 0x0b9ff } }, { { 0x05512, 0x0b808 } }, { { 0x05638, 0x0b617 } },
  { { 0x0575f, 0x0b42a } }, { { 0x05888, 0x0b243 } }, { { 0x059b4, 0x0b061 } }, { { 0x05ae1, 0x0ae83 } },
  { { 0x05c10, 0x0acaa } }, { { 0x05d41, 0x0aad6 } }, { { 0x05e74, 0x0a907 } }, { { 0x05fa9, 0x0a73c } },
  { { 0x060e0, 0x0a575 } }, { { 0x06219, 0x0a3b3 } }, { { 0x06354, 0x0a1f5 } }, { { 0x06491, 0x0a03b } },
  { { 0x065d1, 0x09e85 } }, { { 0x06712, 0x09cd4 } }, { { 0x06856, 0x09b26 } }, { { 0x0699c, 0x0997c } },
  { { 0x06ae4, 0x097d6 } }, { { 0x06c2f, 0x09634 } }, { { 0x06d7c, 0x09495 } }, { { 0x06ecb, 0x092fa } },
  { { 0x0701d, 0x09162 } }, { { 0x07171, 0x08fce } }, { { 0x072c7, 0x08e3e } }, { { 0x07421, 0x08cb0 } },
  { { 0x0757c, 0x08b26 } }, { { 0x076da, 0x089a0 } }, { { 0x0783b, 0x0881c } }, { { 0x0799f, 0x0869c } },
  { { 0x07b05, 0x0851f } }, { { 0x07c6e, 0x083a4 } }, { { 0x07dd9, 0x0822d } }, { { 0x07f48, 0x080b9 } },
  { { 0x080b9, 0x07f48 } }, { { 0x0822d, 0x07dd9 } }, { { 0x083a4, 0x07c6e } }, { { 0x0851f, 0x07b05 } },
  { { 0x0869c, 0x0799f } }, { { 0x0881c, 0x0783b } }, { { 0x089a0, 0x076da } }, { { 0x08b26, 0x0757c } },
  { { 0x08cb0, 0x07421 } }, { { 0x08e3e, 0x072c7 } }, { { 0x08fce, 0x07171 } }, { { 0x09162, 0x0701d } },
  { { 0x092fa, 0x06ecb } }, { { 0x09495, 0x06d7c } }, { { 0x09634, 0x06c2f } }, { { 0x097d6, 0x06ae4 } },
  { { 0x0997c, 0x0699c } }, { { 0x09b26, 0x06856 } }, { { 0x09cd4, 0x06712 } }, { { 0x09e85, 0x065d1 } },
  { { 0x0a03b, 0x06491 } }, { { 0x0a1f5, 0x06354 } }, { { 0x0a3b3, 0x06219 } }, { { 0x0a575, 0x060e0 } },
  { { 0x0a73c, 0x05fa9 } }, { { 0x0a907, 0x05e74 } }, { { 0x0aad6, 0x05d41 } }, { { 0x0acaa, 0x05c10 } },
  { { 0x0ae83, 0x05ae1 } }, { { 0x0b061, 0x059b4 } }, { { 0x0b243, 0x05888 } }, { { 0x0b42a, 0x0575f } },
  { { 0x0b617, 0x05638 } }, { { 0x0b808, 0x05512 } }, { { 0x0b9ff, 0x053ee } }, { { 0x0bbfc, 0x052cc } },
  { { 0x0bdfe, 0x051ac } }, { { 0x0c005, 0x0508d } }, { { 0x0c212, 0x04f71 } }, { { 0x0c426, 0x04e55 } },
  { { 0x0c63f, 0x04d3c } }, { { 0x0c85e, 0x04c24 } }, { { 0x0ca84, 0x04b0e } }, { { 0x0ccb0, 0x049fa } },
  { { 0x0cee3, 0x048e7 } }, { { 0x0d11c, 0x047d6 } }, { { 0x0d35d, 0x046c6 } }, { { 0x0d5a5, 0x045b8 } },
  { { 0x0d7f3, 0x044ab } }, { { 0x0da4a, 0x043a0 } }, { { 0x0dca8, 0x04297 } }, { { 0x0df0e, 0x0418e } },
  { { 0x0e17c, 0x04088 } }, { { 0x0e3f2, 0x03f83 } }, { { 0x0e671, 0x03e7f } }, { { 0x0e8f9, 0x03d7d } },
  { { 0x0eb89, 0x03c7c } }, { { 0x0ee23, 0x03b7c } }, { { 0x0f0c7, 0x03a7e } }, { { 0x0f374, 0x03981 } },
  { { 0x0f62b, 0x03886 } }, { { 0x0f8ed, 0x0378c } }, { { 0x0fbb9, 0x03693 } }, { { 0x0fe90, 0x0359b } },
  { { 0x10173, 0x034a5 } }, { { 0x10461, 0x033b0 } }, { { 0x1075c, 0x032bc } }, { { 0x10a63, 0x031ca } },
  { { 0x10d77, 0x030d9 } }, { { 0x11098, 0x02fe9 } }, { { 0x113c7, 0x02efa } }, { { 0x11704, 0x02e0c } },
  { { 0x11a51, 0x02d20 } }, { { 0x11dac, 0x02c35 } }, { { 0x12118, 0x02b4b } }, { { 0x12494, 0x02a62 } },
  { { 0x12821, 0x0297a } }, { { 0x12bc0, 0x02894 } }, { { 0x12f71, 0x027ae } }, { { 0x13336, 0x026ca } },
  { { 0x1370f, 0x025e6 } }, { { 0x13afd, 0x02504 } }, { { 0x13f01, 0x02423 } }, { { 0x1431b, 0x02343 } },
  { { 0x1474e, 0x02264 } }, { { 0x14b99, 0x02186 } }, { { 0x14fff, 0x020a9 } }, { { 0x15480, 0x01fcd } },
  { { 0x1591e, 0x01ef3 } }, { { 0x15dda, 0x01e19 } }, { { 0x162b6, 0x01d40 } }, { { 0x167b4, 0x01c68 } },
  { { 0x16cd5, 0x01b91 } }, { { 0x1721c, 0x01abb } }, { { 0x1778a, 0x019e6 } }, { { 0x17d23, 0x01912 } },
  { { 0x182e8, 0x0183f } }, { { 0x188de, 0x0176d } }, { { 0x18f06, 0x0169c } }, { { 0x19564, 0x015cc } },
  { { 0x19bfc, 0x014fc } }, { { 0x1a2d4, 0x0142e } }, { { 0x1a9ee, 0x01360 } }, { { 0x1b151, 0x01294 } },
  { { 0x1b903, 0x011c8 } }, { { 0x1c10b, 0x010fd } }, { { 0x1c970, 0x01033 } }, { { 0x1d23c, 0x00f6a } },
  { { 0x1db78, 0x00ea2 } }, { { 0x1e531, 0x00dda } }, { { 0x1ef74, 0x00d13 } }, { { 0x1fa51, 0x00c4e } },
  { { 0x205dd, 0x00b89 } }, { { 0x2122e, 0x00ac4 } }, { { 0x21f60, 0x00a01 } }, { { 0x22d96, 0x0093f } },
  { { 0x23cfc, 0x0087d } }, { { 0x24dc9, 0x007bc } }, { { 0x26044, 0x006fc } }, { { 0x274ce, 0x0063c } },
  { { 0x28beb, 0x0057e } }, { { 0x2a658, 0x004c0 } }, { { 0x2c531, 0x00403 } }, { { 0x2ea40, 0x00346 } },
  { { 0x318a9, 0x0028b } }, { { 0x356cb, 0x001d0 } }, { { 0x3b520, 0x00116 } }, { { 0x48000, 0x0005c } },
};

const uint16_t ProbModelTables::m_inistateToCount[128] = {
  614,   647,   681,   718,   756,   797,   839,   884,   932,   982,   1034,  1089,  1148,  1209,  1274,  1342,
  1414,  1490,  1569,  1653,  1742,  1835,  1933,  2037,  2146,  2261,  2382,  2509,  2643,  2785,  2934,  3091,
  3256,  3430,  3614,  3807,  4011,  4225,  4452,  4690,  4941,  5205,  5483,  5777,  6086,  6412,  6755,  7116,
  7497,  7898,  8320,  8766,  9235,  9729,  10249, 10798, 11375, 11984, 12625, 13300, 14012, 14762, 15551, 16384,
  16384, 17216, 18005, 18755, 19467, 20142, 20783, 21392, 21969, 22518, 23038, 23532, 24001, 24447, 24869, 25270,
  25651, 26012, 26355, 26681, 26990, 27284, 27562, 27826, 28077, 28315, 28542, 28756, 28960, 29153, 29337, 29511,
  29676, 29833, 29982, 30124, 30258, 30385, 30506, 30621, 30730, 30834, 30932, 31025, 31114, 31198, 31277, 31353,
  31425, 31493, 31558, 31619, 31678, 31733, 31785, 31835, 31883, 31928, 31970, 32011, 32049, 32086, 32120, 32153
};

void BinProbModel_Std::init( int qp, int initId )
{
  int slope     = ( ( initId >>  4 )  * 5 ) - 45;
  int offset    = ( ( initId  & 15 ) << 3 ) - 16;
  int inistate  = ( ( slope   * qp ) >> 4 ) + offset;
  const int p1 = m_inistateToCount[inistate < 0 ? 0 : inistate > 127 ? 127 : inistate];
  m_state[0]   = p1 & MASK_0;
  m_state[1]   = p1 & MASK_1;
}




CtxSet::CtxSet( std::initializer_list<CtxSet> ctxSets )
{
  uint16_t  minOffset = std::numeric_limits<uint16_t>::max();
  uint16_t  maxOffset = 0;
  for( auto iter = ctxSets.begin(); iter != ctxSets.end(); iter++ )
  {
    minOffset = std::min<uint16_t>( minOffset, (*iter).Offset              );
    maxOffset = std::max<uint16_t>( maxOffset, (*iter).Offset+(*iter).Size );
  }
  Offset  = minOffset;
  Size    = maxOffset - minOffset;
}





const std::vector<uint8_t>& ContextSetCfg::getInitTable( unsigned initId )
{
  CHECK( initId >= (unsigned)sm_InitTables.size(),
         "Invalid initId (" << initId << "), only " << sm_InitTables.size() << " tables defined." );
  return sm_InitTables[initId];
}


CtxSet ContextSetCfg::addCtxSet( std::initializer_list<std::initializer_list<uint8_t>> initSet2d )
{
  const std::size_t startIdx  = sm_InitTables[0].size();
  const std::size_t numValues = ( *initSet2d.begin() ).size();
        std::size_t setId     = 0;
  for( auto setIter = initSet2d.begin(); setIter != initSet2d.end() && setId < sm_InitTables.size(); setIter++, setId++ )
  {
    const std::initializer_list<uint8_t>& initSet   = *setIter;
    std::vector<uint8_t>&           initTable = sm_InitTables[setId];
    CHECK( initSet.size() != numValues,
           "Number of init values do not match for all sets (" << initSet.size() << " != " << numValues << ")." );
    initTable.resize( startIdx + numValues );
    std::size_t elemId = startIdx;
    for( auto elemIter = ( *setIter ).begin(); elemIter != ( *setIter ).end(); elemIter++, elemId++ )
    {
      initTable[elemId] = *elemIter;
    }
  }
  return CtxSet( (uint16_t)startIdx, (uint16_t)numValues );
}



#define CNU 154 // dummy initialization value for unused context models 'Context model Not Used'
std::vector<std::vector<uint8_t>> ContextSetCfg::sm_InitTables(NUMBER_OF_SLICE_TYPES + 1);

// clang-format off
const CtxSet ContextSetCfg::SplitFlag = ContextSetCfg::addCtxSet
({
  // |-------- do split ctx -------------------|
  { 122, 124, 141, 108, 125, 156, 138, 126, 143, },
  { 93, 139, 171, 124, 125, 141, 139, 141, 158, },
  { 138, 154, 172, 124, 140, 142, 154, 127, 175, },
  { 9, 13, 8, 8, 13, 12, 5, 10, 12, },
});

const CtxSet ContextSetCfg::SplitQtFlag = ContextSetCfg::addCtxSet
({
  { 138, 140, 142, 136, 138, 140, },
  { 139, 126, 142, 107, 138, 125, },
  { 139, 125, 127, 136, 153, 126, },
  { 0, 8, 8, 12, 12, 8, },
});

const CtxSet ContextSetCfg::SplitHvFlag = ContextSetCfg::addCtxSet
({
  { 154, 168, 155, 139, 155, },
  { 169, 168, 170, 153, 170, },
  { 154, 168, 140, 153, 169, },
  { 10, 9, 9, 8, 8, },
});

const CtxSet ContextSetCfg::Split12Flag = ContextSetCfg::addCtxSet
({
  { 154, 140, 154, 140, },
  { 169, 155, 154, 140, },
  { 154, 170, 154, 170, },
  { 12, 12, 12, 12, },
});

const CtxSet ContextSetCfg::SkipFlag = ContextSetCfg::addCtxSet
({
  { 197, 214, 216, },
  { 197, 198, 185, },
  { 40, 138, 154, },
  { 5, 8, 8, },
});

const CtxSet ContextSetCfg::MergeFlag = ContextSetCfg::addCtxSet
({
  { 111, },
  { 111, },
  { 153, },
  { 5, },
});
#if JVET_N0324_REGULAR_MRG_FLAG
const CtxSet ContextSetCfg::RegularMergeFlag = ContextSetCfg::addCtxSet
({
  { 111, 111, },
  { 111, 111, },
  { 153, 153, },
  { 5, 5, },
  });
#endif
const CtxSet ContextSetCfg::MergeIdx = ContextSetCfg::addCtxSet
({
  { 138, },
  { 154, },
  { 153, },
  { 8, },
});

const CtxSet ContextSetCfg::MmvdFlag = ContextSetCfg::addCtxSet
({
  { 120, },
  { 122, },
  { CNU, },
  { 8, },
});

const CtxSet ContextSetCfg::MmvdMergeIdx = ContextSetCfg::addCtxSet
({
  { 154, },
  { 154, },
  { CNU, },
  { 10, },
});

const CtxSet ContextSetCfg::MmvdStepMvpIdx = ContextSetCfg::addCtxSet
({
  { 213, },
  { 244, },
  { CNU, },
  { 1, },
});

const CtxSet ContextSetCfg::PartSize = ContextSetCfg::addCtxSet
({
  {  CNU, CNU, CNU, CNU,},
  {  CNU, CNU, CNU, CNU,},
  {  CNU, CNU, CNU, CNU,},
  { DWS, DWS, DWS, DWS, }
});

const CtxSet ContextSetCfg::PredMode = ContextSetCfg::addCtxSet
({
  { 192, 168, },
  { 165, 139, },
  { CNU, CNU, },
  { 5, 2, },
});

const CtxSet ContextSetCfg::MultiRefLineIdx = ContextSetCfg::addCtxSet
({
  { 90, 212, CNU, },
  { 118, 212, CNU, },
  { 119, 169, CNU, },
  { 8, 8, DWS, },
});

const CtxSet ContextSetCfg::IntraLumaMpmFlag = ContextSetCfg::addCtxSet
({
  { 154, },
  { 154, },
  { 170, },
  { 6, },
});

#if JVET_N0185_UNIFIED_MPM
const CtxSet ContextSetCfg::IntraLumaPlanarFlag = ContextSetCfg::addCtxSet
({

  { 154, 154, },
  { 154, 154, },
  { 154, 154, },
  { 6,   6,   },
});
#endif

const CtxSet ContextSetCfg::IntraChromaPredMode = ContextSetCfg::addCtxSet
({
  {  137, 139, 140,},
  {  138, 139, 169,},
  {  154, 139, 154,},
  {    5,   8,   9,},
});

const CtxSet ContextSetCfg::DeltaQP = ContextSetCfg::addCtxSet
({
  {  154, 154, 154,},
  {  154, 154, 154,},
  {  154, 154, 154,},
  { DWS, DWS, DWS, }
});

const CtxSet ContextSetCfg::InterDir = ContextSetCfg::addCtxSet
({
  { 111, 125, 110, 94, 192, },
  { 126, 111, 110, 94, 208, },
  { CNU, CNU, CNU, CNU, CNU, },
  { 0, 0, 4, 5, 0, },
});

const CtxSet ContextSetCfg::RefPic = ContextSetCfg::addCtxSet
({
  { 125, 139, },
  { 138, 168, },
  { CNU, CNU, },
  { 4, 5, },
});

const CtxSet ContextSetCfg::AffineFlag = ContextSetCfg::addCtxSet
({
  { 179, 169, 171, },
  { 180, 168, 155, },
  { CNU, CNU, CNU, },
  { 8, 5, 4, },
});

const CtxSet ContextSetCfg::AffineType = ContextSetCfg::addCtxSet
({
  { 138, },
  { 153, },
  { CNU, },
  { 4, },
});

const CtxSet ContextSetCfg::AffMergeIdx = ContextSetCfg::addCtxSet
( {
  { 109, },
  { 95, },
  { CNU, },
  { 0, },
} );

const CtxSet ContextSetCfg::GBiIdx = ContextSetCfg::addCtxSet
({
#if JVET_N0286_SIMPLIFIED_GBI_IDX
  { 228, },
  { 242, },
  { CNU, },
  { 4, },
#else
  // 4 ctx for 1st bin; 1 ctx for each of rest bins
  { 228, CNU, CNU, CNU, 125, 155, 175, },
  { 242, CNU, CNU, CNU, 154, 170, 237, },
  { CNU, CNU, CNU, CNU, CNU, CNU, CNU, },
  { 4, DWS, DWS, DWS, 4, 0, 0, },
#endif
});

const CtxSet ContextSetCfg::Mvd = ContextSetCfg::addCtxSet
({
  { 169, 183, },
  { 155, 154, },
  { 141, 156, },
  { 9, 5, },
});

const CtxSet ContextSetCfg::QtRootCbf = ContextSetCfg::addCtxSet
({
  { 109, },
  { 95, },
  { 110, },
  { 4, },
});

const CtxSet ContextSetCfg::QtCbf[] =
{
  ContextSetCfg::addCtxSet
  ({
    { 141, 127, 139, 140, },
    { 142, 127, 139, 140, },
    { CNU, 111, 124, 111, },
    { 1, 5, 9, 8, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 163, 154, CNU, CNU, CNU, },
    { 164, 154, CNU, CNU, CNU, },
    { 109, CNU, CNU, CNU, CNU, },
    { 5, 8, DWS, DWS, DWS, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 161, 154, },
    { 192, 154, },
    { 151, 155, },
    { 5, 5, },
  }),
};

const CtxSet ContextSetCfg::SigCoeffGroup[] =
{
  ContextSetCfg::addCtxSet
  ({
    { 105, 155, },
    { 106, 156, },
    { 107, 158, },
    { 8, 5, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 91, 155, },
    { 90, 141, },
    { 76, 127, },
    { 5, 8, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { CNU, CNU, },
    { CNU, CNU, },
    { CNU, CNU, },
    { DWS, DWS, }
  }),
  ContextSetCfg::addCtxSet
  ({
    { CNU, CNU, },
    { CNU, CNU, },
    { CNU, CNU, },
    { DWS, DWS, }
  }),
};

const CtxSet ContextSetCfg::SigFlag[] =
{
  ContextSetCfg::addCtxSet
  ({
    { 88, 166, 152, 182, 168, 154, 0, 167, 182, 168, 183, 155, 193, 213, 183, 183, 169, 185, },
    { 132, 152, 167, 168, 183, 140, 177, 182, 168, 154, 169, 155, 180, 213, 183, 169, 184, 156, },
    { 89, 138, 153, 139, 154, 140, 134, 139, 139, 140, 140, 141, 137, 170, 169, 170, 141, 157, },
    { 12, 9, 9, 9, 9, 10, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 9, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 72, 167, 153, 168, 154, 155, 180, 199, 183, 199, 199, 186, },
    { 133, 138, 153, 139, 154, 140, 181, 229, 169, 229, 170, 157, },
    { 43, 153, 168, 169, 154, 155, 152, 215, 155, 201, 171, 143, },
    { 9, 9, 12, 9, 13, 13, 5, 5, 8, 8, 8, 9, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 152, 156, 201, 186, 186, 187, 182, 248, 188, 232, 188, 205, 182, 223, 223, 223, 223, 223, },
    { 123, 142, 157, 172, 172, 218, 138, 249, 248, 248, 219, 223, 139, 223, 223, 223, 223, 223, },
    { 93, 142, 157, 143, 188, 175, 138, 238, 205, 238, 253, 237, 139, 223, 223, 223, 223, 253, },
    { 9, 12, 8, 8, 8, 8, 8, 8, 8, 8, 8, 5, 8, 0, 0, 0, 0, 0, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 182, 171, 143, 158, 172, 189, 183, 223, 223, 223, 223, 223, },
    { 168, 156, 173, 216, 172, 219, 169, 223, 223, 223, 223, 223, },
    { 152, 173, 157, 187, 204, 253, 170, 223, 223, 223, 223, 223, },
    { 8, 9, 12, 8, 8, 8, 4, 0, 2, 2, 2, 2, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 123, 173, 223, 191, 232, 251, 212, 223, 223, 236, 206, 223, 192, 223, 223, 223, 223, 223, },
    { 123, 175, 223, 175, 218, 223, 138, 223, 223, 223, 222, 223, 196, 223, 223, 223, 223, 223, },
    { 107, 174, 223, 238, 251, 223, 63, 223, 223, 238, 223, 238, 12, 223, 223, 223, 223, 223, },
    { 8, 8, 4, 8, 8, 8, 8, 0, 0, 4, 8, 5, 4, 2, 2, 2, 2, 1, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 167, 201, 223, 248, 219, 223, 181, 223, 223, 223, 223, 223, },
    { 167, 171, 223, 175, 248, 223, 152, 223, 223, 223, 223, 223, },
    { 166, 234, 223, 236, 248, 223, 108, 223, 223, 223, 223, 223, },
    { 8, 8, 5, 8, 8, 8, 5, 1, 2, 2, 2, 2, },
  }),
};


const CtxSet ContextSetCfg::ParFlag[] =
{
  ContextSetCfg::addCtxSet
  ({
    { 121, 105, 136, 152, 138, 183, 90, 122, 167, 153, 168, 135, 152, 153, 168, 139, 151, 153, 139, 168, 154, },
    { 121, 119, 136, 137, 138, 153, 104, 122, 138, 153, 139, 106, 138, 153, 168, 139, 137, 153, 168, 139, 139, },
    { 121, 135, 137, 152, 138, 153, 91, 137, 138, 153, 139, 151, 138, 153, 139, 139, 138, 168, 139, 154, 139, },
    { 8, 9, 12, 13, 13, 13, 10, 13, 13, 13, 13, 13, 13, 13, 13, 13, 10, 13, 13, 13, 13, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 151, 120, 152, 138, 153, 153, 136, 168, 154, 168, 154, },
    { 135, 120, 137, 138, 138, 153, 136, 153, 168, 139, 154, },
    { 136, 135, 152, 153, 138, 153, 136, 168, 154, 139, 154, },
    { 8, 10, 12, 12, 13, 13, 10, 10, 13, 13, 13, },
  }),
};

const CtxSet ContextSetCfg::GtxFlag[] =
{
  ContextSetCfg::addCtxSet
  ({
    { 31, 73, 118, 75, 152, 109, 42, 44, 105, 107, 109, 0, 119, 136, 152, 124, 118, 136, 138, 153, 140, },
    { 14, 116, 86, 119, 106, 152, 0, 72, 120, 151, 138, 116, 90, 107, 152, 153, 104, 107, 123, 153, 154, },
    { 90, 72, 119, 135, 137, 138, 43, 60, 106, 137, 109, 58, 106, 108, 109, 124, 121, 138, 139, 154, 155, },
    { 4, 1, 8, 8, 4, 2, 5, 9, 9, 8, 9, 9, 9, 9, 8, 9, 9, 8, 9, 8, 8, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 119, 101, 134, 151, 107, 123, 118, 122, 124, 140, 155, },
    { 117, 0, 90, 106, 92, 93, 147, 136, 138, 154, 140, },
    { 194, 40, 120, 122, 122, 138, 103, 121, 153, 154, 155, },
    { 2, 5, 8, 8, 8, 6, 6, 8, 8, 8, 7, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 43, 177, 181, 168, 154, 170, 133, 167, 139, 154, 155, 164, 153, 154, 169, 155, 181, 183, 169, 185, 186, },
    { 101, 133, 137, 153, 139, 140, 134, 138, 139, 154, 155, 136, 153, 154, 140, 170, 138, 154, 155, 170, 186, },
    { 134, 120, 123, 153, 139, 140, 92, 124, 154, 125, 111, 138, 154, 140, 155, 141, 154, 140, 185, 171, 143, },
    { 8, 5, 9, 9, 12, 9, 9, 10, 13, 12, 10, 9, 10, 10, 10, 10, 8, 9, 8, 8, 10, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 0, 178, 153, 154, 140, 140, 196, 170, 186, 157, 188, },
    { 0, 135, 153, 139, 125, 140, 182, 155, 156, 142, 159, },
    { 163, 136, 153, 154, 125, 140, 183, 170, 201, 187, 174, },
    { 6, 9, 10, 12, 12, 10, 5, 9, 8, 8, 9, },
  }),
};

const CtxSet ContextSetCfg::LastX[] =
{
  ContextSetCfg::addCtxSet
  ({
    { 111, 111, 110, 111, 111, 139, 111, 126, 111, 139, 126, 126, 111, 111, 169, 154, 111, 110, 110, 139, CNU, CNU, CNU, CNU, CNU, },
    { 125, 110, 109, 125, 125, 123, 111, 111, 95, 123, 126, 111, 110, 95, 169, 154, 140, 139, 139, 138, CNU, CNU, CNU, CNU, CNU, },
    { 125, 140, 124, 111, 111, 109, 111, 126, 125, 123, 111, 141, 111, 125, 79, 155, 142, 170, 140, 183, CNU, CNU, CNU, CNU, CNU, },
    { 8, 5, 5, 5, 4, 4, 5, 4, 4, 0, 5, 1, 0, 0, 0, 1, 1, 0, 0, 0, DWS, DWS, DWS, DWS, DWS, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 122, 124, 63, CNU, },
    { 138, 123, 92, CNU, },
    { 138, 108, 47, CNU, },
    { 2, 1, 1, DWS, },
  }),
};

const CtxSet ContextSetCfg::LastY[] =
{
  ContextSetCfg::addCtxSet
  ({
    { 125, 125, 139, 125, 111, 139, 111, 111, 110, 110, 140, 126, 125, 125, 140, 139, 111, 110, 124, 181, CNU, CNU, CNU, CNU, CNU, },
    { 95, 95, 109, 110, 110, 108, 125, 111, 124, 123, 140, 111, 110, 124, 139, 125, 126, 110, 124, 182, CNU, CNU, CNU, CNU, CNU, },
    { 110, 110, 109, 125, 111, 123, 111, 126, 95, 108, 111, 127, 111, 95, 78, 169, 157, 141, 125, 138, CNU, CNU, CNU, CNU, CNU, },
    { 8, 5, 8, 5, 5, 4, 5, 5, 4, 0, 5, 5, 1, 0, 0, 1, 4, 1, 0, 0, DWS, DWS, DWS, DWS, DWS, },
  }),
  ContextSetCfg::addCtxSet
  ({
    { 122, 124, 123, CNU, },
    { 108, 123, 121, CNU, },
    { 123, 123, 91, CNU, },
    { 2, 2, 2, DWS, },
  }),
};


const CtxSet ContextSetCfg::MVPIdx = ContextSetCfg::addCtxSet
({
  { 153, },
  { 168, },
  { 168, },
  { 10, },
});

const CtxSet ContextSetCfg::SmvdFlag = ContextSetCfg::addCtxSet
( {
  { 154, },
  { 125, },
  { CNU, },
  { 8, },
} );

const CtxSet ContextSetCfg::SaoMergeFlag = ContextSetCfg::addCtxSet
({
  { 47, },
  { 244, },
  { 199, },
  { 0, },
});

const CtxSet ContextSetCfg::SaoTypeIdx = ContextSetCfg::addCtxSet
({
  { 47, },
  { 95, },
  { 95, },
  { 0, },
});


const CtxSet ContextSetCfg::TransquantBypassFlag = ContextSetCfg::addCtxSet
({
  {  154,},
  {  154,},
  {  154,},
  { DWS, }
});

const CtxSet ContextSetCfg::RdpcmFlag = ContextSetCfg::addCtxSet
({
  {  139, 139,},
  {  139, 139,},
  {  CNU, CNU,},
  { DWS, DWS, }
});

const CtxSet ContextSetCfg::RdpcmDir = ContextSetCfg::addCtxSet
({
  {  139, 139,},
  {  139, 139,},
  {  CNU, CNU,},
  { DWS, DWS, }
});

const CtxSet ContextSetCfg::MTSIndex = ContextSetCfg::addCtxSet
({
  { CNU, 155, 155, 140, 140, CNU, 216, 153, 153, 0, CNU, },
  { CNU, 155, 155, 140, 140, CNU, 233, 167, 153, 0, CNU, },
  { CNU, CNU, 140, 140, 140, CNU, 219, 138, 153, 0, CNU, },
  { DWS, 8, 8, 8, 8, DWS, 4, 8, 9, 3, DWS, },
});

const CtxSet ContextSetCfg::ISPMode = ContextSetCfg::addCtxSet
({
  { 152, 154, },
  { 166, 154, },
  { 152, 154, },
  { 8, 5, },
});

const CtxSet ContextSetCfg::SbtFlag = ContextSetCfg::addCtxSet
( {
  { 168, 183, },
  { 197, 183, },
  { CNU, CNU, },
  { 4, 8, },
} );

const CtxSet ContextSetCfg::SbtQuadFlag = ContextSetCfg::addCtxSet
( {
  { 168, },
  { 168, },
  { CNU, },
  { 9, },
} );

const CtxSet ContextSetCfg::SbtHorFlag = ContextSetCfg::addCtxSet
( {
  { 139, 154, 139, },
  { 139, 154, 139, },
  { CNU, CNU, CNU, },
  { 8, 5, 4, },
} );

const CtxSet ContextSetCfg::SbtPosFlag = ContextSetCfg::addCtxSet
( {
  { 154, },
  { 154, },
  { CNU, },
  { 13, },
} );

const CtxSet ContextSetCfg::CrossCompPred = ContextSetCfg::addCtxSet
({
  {  154, 154, 154, 154, 154, 154, 154, 154, 154, 154,},
  {  154, 154, 154, 154, 154, 154, 154, 154, 154, 154,},
  {  154, 154, 154, 154, 154, 154, 154, 154, 154, 154,},
  { DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, }
});

const CtxSet ContextSetCfg::ChromaQpAdjFlag = ContextSetCfg::addCtxSet
({
  {  154,},
  {  154,},
  {  154,},
  { DWS, }
});

const CtxSet ContextSetCfg::ChromaQpAdjIdc = ContextSetCfg::addCtxSet
({
  {  154,},
  {  154,},
  {  154,},
  { DWS, }
});

const CtxSet ContextSetCfg::ImvFlag = ContextSetCfg::addCtxSet
({
#if JVET_N600_AMVR_TPM_CTX_REDUCTION
  { 212, 180, 183, 242, },
  { 213, 166, 198, 244, },
  { CNU, 152, CNU, CNU, },
  { 1, 5, 1, 0, },
#else
  { 212, 199, 215, 180, 183, 242, },
  { 213, 229, 244, 166, 198, 244, },
  { CNU, CNU, CNU, 152, CNU, CNU, },
  { 1, 4, 4, 5, 1, 0, },
#endif
});

const CtxSet ContextSetCfg::ctbAlfFlag =
{
  ContextSetCfg::addCtxSet
  ( {
    { 154, 186, 174, 183, 233, 250, 168, 248, 250, },
    { 139, 186, 203, 183, 247, 249, 183, 232, 249, },
    { 219, 236, 238, 232, 249, 235, 246, 234, 251, },
    { 0, 0, 4, 0, 0, 1, 0, 0, 1, },
  } )
};

const CtxSet ContextSetCfg::MHIntraFlag = ContextSetCfg::addCtxSet
({
  { 225, },
  { 197, },
  { CNU, },
  { 1, },
});
#if !JVET_N0302_SIMPLFIED_CIIP
const CtxSet ContextSetCfg::MHIntraPredMode = ContextSetCfg::addCtxSet
({
  { 156, CNU, CNU, CNU, },
  { 156, CNU, CNU, CNU, },
  { CNU, CNU, CNU, CNU, },
  { 9, DWS, DWS, DWS, },
});
#endif
const CtxSet ContextSetCfg::TriangleFlag = ContextSetCfg::addCtxSet
({
#if JVET_N600_AMVR_TPM_CTX_REDUCTION
  { 149, },
  { 151, },
  { CNU, },
  { 8, },
#else
  { 149, 123, 123, },
  { 151, 152, 138, },
  { CNU, CNU, CNU, },
  { 8, 12, 9, },
#endif
});

const CtxSet ContextSetCfg::TriangleIdx = ContextSetCfg::addCtxSet
({
  { CNU, },
  { CNU, },
  { CNU, },
  { DWS, },
});
// clang-format on

const CtxSet ContextSetCfg::IBCFlag = ContextSetCfg::addCtxSet
({
  { 0, 154, 141, },
  { 0, 153, 140, },
  { 132, 153, 125, },
  { 5, 5, 8, },
});

#if JVET_N0054_JOINT_CHROMA
const CtxSet ContextSetCfg::JointCbCrFlag = ContextSetCfg::addCtxSet
({
  { CNU, },
  { CNU, },
  { CNU, },
  { DWS, },
});
#endif

#if JVET_N0280_RESIDUAL_CODING_TS
const CtxSet ContextSetCfg::TsSigCoeffGroup =
{
  ContextSetCfg::addCtxSet
  ({
    {  CNU, CNU, CNU,  },
    {  CNU, CNU, CNU,  },
    {  CNU, CNU, CNU,  },
    {  DWS, DWS, DWS,  },
  }),
};

const CtxSet ContextSetCfg::TsSigFlag =
{
  ContextSetCfg::addCtxSet
  ({
    {  CNU, CNU, CNU, },
    {  CNU, CNU, CNU, },
    {  CNU, CNU, CNU, },
    {  DWS, DWS, DWS, },
  }),
};

const CtxSet ContextSetCfg::TsParFlag =
{
  ContextSetCfg::addCtxSet
  ({
    {  CNU  },
    {  CNU  },
    {  CNU  },
    {  DWS  },
  }),
};

const CtxSet ContextSetCfg::TsGtxFlag =
{
  ContextSetCfg::addCtxSet
  ({
    {  CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU,  },
    {  CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU,  },
    {  CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU, CNU,  },
    {  DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS, DWS,  },
  }),
};

const CtxSet ContextSetCfg::TsResidualSign =
{
  ContextSetCfg::addCtxSet
  ({
    {  CNU,  },
    {  CNU,  },
    {  CNU,  },
    {  DWS,  },
   }),
};
#endif

const unsigned ContextSetCfg::NumberOfContexts = (unsigned)ContextSetCfg::sm_InitTables[0].size();


// combined sets
const CtxSet ContextSetCfg::Sao = { ContextSetCfg::SaoMergeFlag, ContextSetCfg::SaoTypeIdx };



template <class BinProbModel>
CtxStore<BinProbModel>::CtxStore()
  : m_CtxBuffer ()
  , m_Ctx       ( nullptr )
{}

template <class BinProbModel>
CtxStore<BinProbModel>::CtxStore( bool dummy )
  : m_CtxBuffer ( ContextSetCfg::NumberOfContexts )
  , m_Ctx       ( m_CtxBuffer.data() )
{}

template <class BinProbModel>
CtxStore<BinProbModel>::CtxStore( const CtxStore<BinProbModel>& ctxStore )
  : m_CtxBuffer ( ctxStore.m_CtxBuffer )
  , m_Ctx       ( m_CtxBuffer.data() )
{}

template <class BinProbModel>
void CtxStore<BinProbModel>::init( int qp, int initId )
{
  const std::vector<uint8_t>& initTable = ContextSetCfg::getInitTable( initId );
  CHECK( m_CtxBuffer.size() != initTable.size(),
        "Size of init table (" << initTable.size() << ") does not match size of context buffer (" << m_CtxBuffer.size() << ")." );
  const std::vector<uint8_t> &rateInitTable = ContextSetCfg::getInitTable(NUMBER_OF_SLICE_TYPES);
  CHECK(m_CtxBuffer.size() != rateInitTable.size(),
        "Size of rate init table (" << rateInitTable.size() << ") does not match size of context buffer ("
                                    << m_CtxBuffer.size() << ").");
  int clippedQP = Clip3( 0, MAX_QP, qp );
  for( std::size_t k = 0; k < m_CtxBuffer.size(); k++ )
  {
    m_CtxBuffer[k].init( clippedQP, initTable[k] );
    m_CtxBuffer[k].setLog2WindowSize(rateInitTable[k]);
  }
}

template <class BinProbModel>
void CtxStore<BinProbModel>::setWinSizes( const std::vector<uint8_t>& log2WindowSizes )
{
  CHECK( m_CtxBuffer.size() != log2WindowSizes.size(),
        "Size of window size table (" << log2WindowSizes.size() << ") does not match size of context buffer (" << m_CtxBuffer.size() << ")." );
  for( std::size_t k = 0; k < m_CtxBuffer.size(); k++ )
  {
    m_CtxBuffer[k].setLog2WindowSize( log2WindowSizes[k] );
  }
}

template <class BinProbModel>
void CtxStore<BinProbModel>::loadPStates( const std::vector<uint16_t>& probStates )
{
  CHECK( m_CtxBuffer.size() != probStates.size(),
        "Size of prob states table (" << probStates.size() << ") does not match size of context buffer (" << m_CtxBuffer.size() << ")." );
  for( std::size_t k = 0; k < m_CtxBuffer.size(); k++ )
  {
    m_CtxBuffer[k].setState( probStates[k] );
  }
}

template <class BinProbModel>
void CtxStore<BinProbModel>::savePStates( std::vector<uint16_t>& probStates ) const
{
  probStates.resize( m_CtxBuffer.size(), uint16_t(0) );
  for( std::size_t k = 0; k < m_CtxBuffer.size(); k++ )
  {
    probStates[k] = m_CtxBuffer[k].getState();
  }
}





template class CtxStore<BinProbModel_Std>;





Ctx::Ctx()                                  : m_BPMType( BPM_Undefined )                        {}
Ctx::Ctx( const BinProbModel_Std*   dummy ) : m_BPMType( BPM_Std   ), m_CtxStore_Std  ( true )  {}

Ctx::Ctx( const Ctx& ctx )
  : m_BPMType         ( ctx.m_BPMType )
  , m_CtxStore_Std    ( ctx.m_CtxStore_Std    )
{
  ::memcpy( m_GRAdaptStats, ctx.m_GRAdaptStats, sizeof( unsigned ) * RExt__GOLOMB_RICE_ADAPTATION_STATISTICS_SETS );
}

