// SPDX-License-Identifier: Apache-2.0

#ifndef PXG_SDF_LINEAR_GRID_H
#define PXG_SDF_LINEAR_GRID_H

#include "foundation/PxSimpleTypes.h"

namespace physx
{
	// ROCm builds keep each SDF grid in linear device memory and sample it in software
	// (pxgSampleSdfTexture3D in sdfCollision.cuh). The texture-object slot of a mesh then
	// carries the device address of one of these descriptors instead of a texture object.
	struct PxgSdfLinearGrid
	{
		enum Format
		{
			eFLOAT32 = 0,
			eUNORM8 = 1,	// read as value / 255, like a normalized-float texture read
			eUNORM16 = 2	// read as value / 65535
		};

		const void*	data;
		PxU32		width;
		PxU32		height;
		PxU32		depth;
		PxU32		format;
	};
}

#endif
