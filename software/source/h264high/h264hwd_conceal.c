/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#include "h264hwd_container.h"
#include "h264hwd_conceal.h"
#include "h264hwd_util.h"
#include "h264hwd_dpb.h"

/*------------------------------------------------------------------------------
    Function name : h264bsdConceal
    Description   : Performe error concelament for an erroneous picture

    Return type   : void
    Argument      : storage_t *pStorage
    Argument      : DecAsicBuffers_t * pAsicBuff
    Argument      : u32 sliceType
------------------------------------------------------------------------------*/
void h264bsdConceal(storage_t * pStorage, DecAsicBuffers_t * pAsicBuff,
                    u32 sliceType)
{
    u32 i, tmp;
    i32 refID;

    u32 *pAsicCtrl = pAsicBuff->mbCtrl.virtualAddress;
    mbStorage_t *pMb = pStorage->mb;

    ASSERT(pStorage);
    ASSERT(pAsicBuff);

    DEBUG_PRINT(("h264bsdConceal: Concealing %s slice\n",
                 IS_I_SLICE(sliceType) ? "intra" : "inter"));

    refID = -1;

    /* use reference picture with smallest available index */
    if(IS_P_SLICE(sliceType))
    {
        i = 0;
        do
        {
            refID = h264bsdGetRefPicData(pStorage->dpb, i);
            i++;
        }
        while(i < 16 && refID == -1);
    }

    i = 0;
    /* find first properly decoded macroblock -> start point for concealment */
    while(i < pStorage->picSizeInMbs && !pMb[i].decoded)
    {
        i++;
    }

    /* whole picture lost -> copy previous or set grey */
    if(i == pStorage->picSizeInMbs)
    {
        DEBUG_PRINT(("h264bsdConceal: whole picture lost\n"));
        if(IS_I_SLICE(sliceType) || refID == -1)
        {
            /* QP = 40 and all residual blocks empty */
            /* DC predi. & chroma DC pred. --> grey */
            tmp = ((u32) HW_I_16x16 << 29) | (2U << 27) | (40U << 15) | 0x7F;

            for(i = pStorage->picSizeInMbs; i > 0; i--)
            {
                *pAsicCtrl++ = tmp;
                *pAsicCtrl++ = 0;
            }

            pMb->mbType_asic = I_16x16_2_0_0;
        }
        else
        {
            /* QP = 40 and all residual blocks empty */
            tmp = ((u32) HW_P_SKIP << 29) | (40U << 15) | 0x7F;

            for(i = pStorage->picSizeInMbs; i > 0; i--)
            {
                *pAsicCtrl++ = tmp;
                *pAsicCtrl++ = 0;
            }

            /* inter prediction using zero mv and the newest reference */
            pMb->mbType_asic = P_Skip;
            pMb->refID[0] = refID;
        }

        pStorage->numConcealedMbs = pStorage->picSizeInMbs;

        return;
    }

    for(i = 0; i < pStorage->picSizeInMbs; i++, pMb++)
    {
        /* if mb was not correctly decoded, set parameters for concealment */
        if(!pMb->decoded)
        {
            DEBUG_PRINT(("h264bsdConceal: Concealing mb #%u\n", i));

            pStorage->numConcealedMbs++;

            /* intra pred. if it is an intra mb or if the newest reference image
             * is not available */
            if(IS_I_SLICE(sliceType) || refID == -1)
            {
                /* QP = 40 and all residual blocks empty */
                /* DC predi. & chroma DC pred. --> grey */
                tmp =
                    ((u32) HW_I_16x16 << 29) | (2U << 27) | (40U << 15) | 0x7F;

                *pAsicCtrl++ = tmp;
                *pAsicCtrl++ = 0;

                pMb->mbType_asic = I_16x16_2_0_0;
            }
            else
            {
                DEBUG_PRINT(("h264bsdConceal: NOT I slice conceal \n"));
                /* QP = 40 and all residual blocks empty */
                tmp = ((u32) HW_P_SKIP << 29) | (40U << 15) | 0x7F;
                *pAsicCtrl++ = tmp;
                *pAsicCtrl++ = 0;

                /* inter prediction using zero mv and the newest reference */
                pMb->mbType_asic = P_Skip;
                pMb->refID[0] = refID;
            }
        }
        else
            pAsicCtrl += 2;
    }
}
