// SPDX-License-Identifier: Apache-2.0

// ****************************************************************************
// This snippet checks GPU simulation results. It runs a set of fixed scenes with
// GPU dynamics and compares them with the same scenes simulated on the CPU where
// the CPU supports the feature (rigid bodies, articulations, SDF triangle meshes),
// and with physical invariants where it does not (particles, deformable volumes).
// Every check prints PASS or FAIL; the snippet exits with a nonzero status if any
// check fails or PhysX reports an error, including when no GPU context can be
// created (PhysX cannot tell a missing GPU from a GPU library that failed to load).
// ****************************************************************************

#include <stdio.h>
#include <string.h>
#include "PxPhysicsAPI.h"
#include "extensions/PxParticleExt.h"
#include "extensions/PxCudaHelpersExt.h"
#include "extensions/PxDeformableVolumeExt.h"
#include "extensions/PxRemeshingExt.h"
#include "../snippetutils/SnippetUtils.h"

using namespace physx;

namespace
{

class ErrorCounter : public PxErrorCallback
{
public:
	ErrorCounter() : mErrors(0), mOverflowReports(0), mExpectOverflow(false) {}

	virtual void reportError(PxErrorCode::Enum code, const char* message, const char* file, int line) PX_OVERRIDE
	{
		if (code == PxErrorCode::eDEBUG_INFO || code == PxErrorCode::eDEBUG_WARNING || code == PxErrorCode::ePERF_WARNING)
			return;
		if (mExpectOverflow && code == PxErrorCode::eINTERNAL_ERROR && strstr(message, "buffer overflow detected"))
		{
			mOverflowReports++;
			return;
		}
		mErrors++;
		printf("PhysX error %d: %s (%s:%d)\n", int(code), message, file, line);
	}

	PxU32 mErrors;
	PxU32 mOverflowReports;
	bool mExpectOverflow;
};

PxDefaultAllocator		gAllocator;
ErrorCounter			gErrorCallback;
PxFoundation*			gFoundation			= NULL;
PxPhysics*				gPhysics			= NULL;
PxCudaContextManager*	gCudaContextManager	= NULL;
PxDefaultCpuDispatcher*	gDispatcher			= NULL;
PxMaterial*				gMaterial			= NULL;
PxU32					gChecks				= 0;
PxU32					gFailures			= 0;

const PxReal			gDt					= 1.0f / 60.0f;

void check(bool ok, const char* scene, const char* what, PxReal value, PxReal limit)
{
	gChecks++;
	if (!ok)
		gFailures++;
	printf("%s  %-22s %-46s %12.6g  (limit %g)\n", ok ? "PASS" : "FAIL", scene, what, double(value), double(limit));
}

bool isFinite(const PxVec3& v)
{
	return PxIsFinite(v.x) && PxIsFinite(v.y) && PxIsFinite(v.z);
}

PxScene* createScene(bool gpu, const PxGpuDynamicsMemoryConfig* gpuConfig = NULL)
{
	PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
	if (gpuConfig)
		sceneDesc.gpuDynamicsConfig = *gpuConfig;
	sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
	sceneDesc.cpuDispatcher = gDispatcher;
	sceneDesc.filterShader = PxDefaultSimulationFilterShader;
	sceneDesc.solverType = PxSolverType::eTGS;
	sceneDesc.flags |= PxSceneFlag::eENABLE_PCM;
	if (gpu)
	{
		sceneDesc.cudaContextManager = gCudaContextManager;
		sceneDesc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
		sceneDesc.broadPhaseType = PxBroadPhaseType::eGPU;
		sceneDesc.gpuMaxNumPartitions = 8;
	}
	PxScene* scene = gPhysics->createScene(sceneDesc);
	if (gpu && !(scene->getFlags() & PxSceneFlag::eENABLE_GPU_DYNAMICS))
		check(false, "setup", "GPU dynamics enabled on the GPU scene", 0.0f, 1.0f);
	return scene;
}

void simulate(PxScene* scene, PxU32 frames)
{
	for (PxU32 i = 0; i < frames; i++)
	{
		scene->simulate(gDt);
		scene->fetchResults(true);
	}
}

PxRigidDynamic* addBox(PxScene* scene, const PxTransform& pose, const PxVec3& halfExtents, PxReal density = 1000.0f)
{
	PxRigidDynamic* body = PxCreateDynamic(*gPhysics, pose, PxBoxGeometry(halfExtents), *gMaterial, density);
	body->setSolverIterationCounts(16, 1);
	scene->addActor(*body);
	return body;
}

void addGround(PxScene* scene)
{
	scene->addActor(*PxCreatePlane(*gPhysics, PxPlane(0.0f, 1.0f, 0.0f, 0.0f), *gMaterial));
}

PxReal maxDistance(const PxArray<PxVec3>& a, const PxArray<PxVec3>& b)
{
	PxReal d = (a.size() == b.size()) ? 0.0f : PX_MAX_F32;
	for (PxU32 i = 0; i < a.size() && i < b.size(); i++)
		d = PxMax(d, (a[i] - b[i]).magnitude());
	return d;
}

bool allFinite(const PxArray<PxVec3>& a)
{
	for (PxU32 i = 0; i < a.size(); i++)
		if (!isFinite(a[i]))
			return false;
	return true;
}

// Triangle mesh of an axis-aligned box, two triangles per face, outward winding.
void boxMesh(const PxVec3& center, const PxVec3& halfExtents, PxArray<PxVec3>& verts, PxArray<PxU32>& indices)
{
	static const PxU32 sIndices[36] = { 1,2,3, 7,6,5, 4,5,1, 5,6,2, 2,6,7, 0,3,7, 0,1,3, 4,7,5, 0,4,1, 1,5,2, 3,2,7, 4,0,7 };
	const PxVec3 corners[8] = {
		PxVec3(1.0f, -1.0f, -1.0f), PxVec3(1.0f, -1.0f, 1.0f), PxVec3(-1.0f, -1.0f, 1.0f), PxVec3(-1.0f, -1.0f, -1.0f),
		PxVec3(1.0f, 1.0f, -1.0f), PxVec3(1.0f, 1.0f, 1.0f), PxVec3(-1.0f, 1.0f, 1.0f), PxVec3(-1.0f, 1.0f, -1.0f) };
	verts.clear();
	indices.clear();
	for (PxU32 i = 0; i < 8; i++)
		verts.pushBack(center + corners[i].multiply(halfExtents));
	for (PxU32 i = 0; i < 36; i++)
		indices.pushBack(sIndices[i]);
}

// The same box with each face split into n x n quads. Shared edge vertices are computed
// identically on every face so that cooking welds them.
void tessellatedBoxMesh(const PxVec3& center, const PxVec3& halfExtents, PxU32 n, PxArray<PxVec3>& verts, PxArray<PxU32>& indices)
{
	verts.clear();
	indices.clear();
	for (PxU32 axis = 0; axis < 3; axis++)
	{
		for (PxU32 side = 0; side < 2; side++)
		{
			const PxU32 u = (axis + 1) % 3, v = (axis + 2) % 3;
			const PxU32 base = verts.size();
			for (PxU32 j = 0; j <= n; j++)
			{
				for (PxU32 i = 0; i <= n; i++)
				{
					PxVec3 p;
					p[axis] = side ? 1.0f : -1.0f;
					p[u] = -1.0f + 2.0f * PxReal(i) / PxReal(n);
					p[v] = -1.0f + 2.0f * PxReal(j) / PxReal(n);
					verts.pushBack(center + p.multiply(halfExtents));
				}
			}
			for (PxU32 j = 0; j < n; j++)
			{
				for (PxU32 i = 0; i < n; i++)
				{
					const PxU32 a = base + j * (n + 1) + i, b = a + 1, c = a + n + 1, d = c + 1;
					const PxU32 quad[2][6] = { { a, d, b, a, c, d }, { a, b, d, a, d, c } };
					for (PxU32 k = 0; k < 6; k++)
						indices.pushBack(quad[side][k]);
				}
			}
		}
	}
}

// sdfSpacing 0 cooks no SDF; subgridSize 0 cooks a dense SDF.
PxTriangleMesh* cookMesh(const PxArray<PxVec3>& verts, const PxArray<PxU32>& indices, PxReal sdfSpacing, PxU32 subgridSize,
	PxSdfBitsPerSubgridPixel::Enum bits)
{
	PxTriangleMeshDesc meshDesc;
	meshDesc.points.count = verts.size();
	meshDesc.points.stride = sizeof(PxVec3);
	meshDesc.points.data = verts.begin();
	meshDesc.triangles.count = indices.size() / 3;
	meshDesc.triangles.stride = 3 * sizeof(PxU32);
	meshDesc.triangles.data = indices.begin();

	PxSDFDesc sdfDesc;
	if (sdfSpacing > 0.0f)
	{
		sdfDesc.spacing = sdfSpacing;
		sdfDesc.subgridSize = subgridSize;
		sdfDesc.bitsPerSubgridPixel = bits;
		meshDesc.sdfDesc = &sdfDesc;
	}

	PxCookingParams params(gPhysics->getTolerancesScale());
	params.meshPreprocessParams |= PxMeshPreprocessingFlag::eENABLE_INERTIA;
	params.meshPreprocessParams |= PxMeshPreprocessingFlag::eWELD_VERTICES;
	params.meshWeldTolerance = 1e-7f;
	params.buildGPUData = true;
	return PxCreateTriangleMesh(params, meshDesc, gPhysics->getPhysicsInsertionCallback());
}

void copyDeviceToHost(void* dst, const void* src, PxU32 bytes)
{
	PxScopedCudaLock lock(*gCudaContextManager);
	gCudaContextManager->getCudaContext()->memcpyDtoH(dst, reinterpret_cast<CUdeviceptr>(src), bytes);
}

// ---------------------------------------------------------------------------
// Rigid bodies in free fall with initial linear and angular velocity, no contacts.
void freeFall(bool gpu, PxArray<PxVec3>& positions, PxArray<PxVec3>& axes)
{
	PxScene* scene = createScene(gpu);
	PxArray<PxRigidDynamic*> bodies;
	for (PxU32 i = 0; i < 16; i++)
	{
		const PxReal f = PxReal(i);
		PxRigidDynamic* body = addBox(scene, PxTransform(PxVec3(f * 2.0f, 10.0f + f, 0.0f), PxQuat(0.1f * f, PxVec3(0.0f, 0.0f, 1.0f))), PxVec3(0.25f, 0.5f, 0.75f));
		body->setLinearVelocity(PxVec3(0.5f * f, 1.0f, -0.25f * f));
		body->setAngularVelocity(PxVec3(0.3f, 0.2f * f, 0.1f));
		body->setAngularDamping(0.0f);
		bodies.pushBack(body);
	}
	simulate(scene, 30);
	for (PxU32 i = 0; i < bodies.size(); i++)
	{
		const PxTransform pose = bodies[i]->getGlobalPose();
		positions.pushBack(pose.p);
		axes.pushBack(pose.q.getBasisVector0());
	}
	scene->release();
}

// Box pyramids and a box tower settling on the ground (GPU broadphase, contacts, solver).
void stacks(bool gpu, PxArray<PxVec3>& positions, PxReal& topHeight, PxReal& maxSpeed)
{
	PxScene* scene = createScene(gpu);
	addGround(scene);
	const PxReal h = 0.25f;
	PxArray<PxRigidDynamic*> bodies;
	for (PxU32 s = 0; s < 3; s++)
	{
		const PxReal z = PxReal(s) * 3.0f;
		for (PxU32 level = 0; level < 5; level++)
			for (PxU32 j = 0; j < 5 - level; j++)
				bodies.pushBack(addBox(scene, PxTransform(PxVec3((PxReal(j) - (4 - level) * 0.5f) * 2.0f * h, h + level * 2.0f * h, z)), PxVec3(h)));
	}
	PxRigidDynamic* top = NULL;
	for (PxU32 level = 0; level < 6; level++)
	{
		top = addBox(scene, PxTransform(PxVec3(-4.0f, h + level * 2.0f * h, 0.0f)), PxVec3(h));
		bodies.pushBack(top);
	}
	simulate(scene, 300);
	maxSpeed = 0.0f;
	for (PxU32 i = 0; i < bodies.size(); i++)
	{
		positions.pushBack(bodies[i]->getGlobalPose().p);
		maxSpeed = PxMax(maxSpeed, bodies[i]->getLinearVelocity().magnitude());
	}
	topHeight = top->getGlobalPose().p.y;
	scene->release();
}

// A three-link floating articulation lying on the ground with boxes dropped on each link.
void articulationContacts(bool gpu, PxArray<PxVec3>& positions)
{
	PxScene* scene = createScene(gpu);
	addGround(scene);
	PxArticulationReducedCoordinate* articulation = gPhysics->createArticulationReducedCoordinate();
	articulation->setSolverIterationCounts(32, 1);
	PxArray<PxArticulationLink*> links;
	PxArticulationLink* parent = NULL;
	for (PxU32 i = 0; i < 3; i++)
	{
		PxArticulationLink* link = articulation->createLink(parent, PxTransform(PxVec3(PxReal(i) * 1.1f, 0.1f, 0.0f)));
		PxRigidActorExt::createExclusiveShape(*link, PxBoxGeometry(0.5f, 0.1f, 0.5f), *gMaterial);
		PxRigidBodyExt::updateMassAndInertia(*link, 500.0f);
		if (parent)
		{
			PxArticulationJointReducedCoordinate* joint = link->getInboundJoint();
			joint->setJointType(PxArticulationJointType::eREVOLUTE);
			joint->setMotion(PxArticulationAxis::eTWIST, PxArticulationMotion::eLIMITED);
			joint->setLimitParams(PxArticulationAxis::eTWIST, PxArticulationLimit(-0.5f, 0.5f));
			joint->setParentPose(PxTransform(PxVec3(0.55f, 0.0f, 0.0f), PxQuat(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f))));
			joint->setChildPose(PxTransform(PxVec3(-0.55f, 0.0f, 0.0f), PxQuat(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f))));
		}
		links.pushBack(link);
		parent = link;
	}
	scene->addArticulation(*articulation);
	PxArray<PxRigidDynamic*> boxes;
	for (PxU32 i = 0; i < 3; i++)
		boxes.pushBack(addBox(scene, PxTransform(PxVec3(PxReal(i) * 1.1f, 0.6f + 0.3f * PxReal(i), 0.0f)), PxVec3(0.2f)));
	simulate(scene, 300);
	for (PxU32 i = 0; i < links.size(); i++)
		positions.pushBack(links[i]->getGlobalPose().p);
	for (PxU32 i = 0; i < boxes.size(); i++)
		positions.pushBack(boxes[i]->getGlobalPose().p);
	scene->release();
}

// Dynamic SDF triangle-mesh cubes (dense, and sparse with 8, 16 and 32 bits per subgrid
// texel) resting on a static triangle-mesh table without an SDF, so every contact comes
// from sampling the cubes' SDFs.
const PxReal gTableTop = 1.0f;
const PxReal gCubeHalf = 0.25f;

void sdfMeshes(bool gpu, PxArray<PxVec3>& positions)
{
	PxScene* scene = createScene(gpu);
	addGround(scene);

	PxArray<PxVec3> verts;
	PxArray<PxU32> indices;
	boxMesh(PxVec3(0.0f, gTableTop - 0.5f, 0.0f), PxVec3(5.0f, 0.5f, 5.0f), verts, indices);
	PxTriangleMesh* tableMesh = cookMesh(verts, indices, 0.0f, 0, PxSdfBitsPerSubgridPixel::e16_BIT_PER_PIXEL);
	PxRigidStatic* table = gPhysics->createRigidStatic(PxTransform(PxIdentity));
	PxRigidActorExt::createExclusiveShape(*table, PxTriangleMeshGeometry(tableMesh), *gMaterial);
	scene->addActor(*table);

	const PxU32 subgrids[4] = { 0, 4, 4, 4 };
	const PxSdfBitsPerSubgridPixel::Enum bits[4] = { PxSdfBitsPerSubgridPixel::e32_BIT_PER_PIXEL, PxSdfBitsPerSubgridPixel::e8_BIT_PER_PIXEL,
		PxSdfBitsPerSubgridPixel::e16_BIT_PER_PIXEL, PxSdfBitsPerSubgridPixel::e32_BIT_PER_PIXEL };
	PxArray<PxRigidDynamic*> cubes;
	for (PxU32 i = 0; i < 4; i++)
	{
		boxMesh(PxVec3(0.0f), PxVec3(gCubeHalf), verts, indices);
		PxTriangleMesh* mesh = cookMesh(verts, indices, 0.02f, subgrids[i], bits[i]);
		PxRigidDynamic* cube = gPhysics->createRigidDynamic(PxTransform(PxVec3(-1.5f + PxReal(i), gTableTop + gCubeHalf + 0.3f, 0.0f), PxQuat(0.2f, PxVec3(0.0f, 1.0f, 0.0f))));
		PxShape* shape = PxRigidActorExt::createExclusiveShape(*cube, PxTriangleMeshGeometry(mesh), *gMaterial);
		shape->setContactOffset(0.02f);
		shape->setRestOffset(0.0f);
		PxRigidBodyExt::updateMassAndInertia(*cube, 500.0f);
		cube->setSolverIterationCounts(16, 1);
		scene->addActor(*cube);
		cubes.pushBack(cube);
		mesh->release();
	}
	tableMesh->release();
	simulate(scene, 240);
	for (PxU32 i = 0; i < cubes.size(); i++)
		positions.pushBack(cubes[i]->getGlobalPose().p);
	scene->release();
}

// A finely tessellated dynamic SDF cube resting on a finely tessellated triangle-mesh
// table. Both meshes have enough triangles that the root of each GPU bounding-volume tree
// has all 32 children, which the mesh-versus-mesh overlap test has to handle.
void largeSdfMesh(bool gpu, PxVec3& position)
{
	PxScene* scene = createScene(gpu);
	addGround(scene);

	PxArray<PxVec3> verts;
	PxArray<PxU32> indices;
	tessellatedBoxMesh(PxVec3(0.0f, gTableTop - 0.5f, 0.0f), PxVec3(2.0f, 0.5f, 2.0f), 24, verts, indices);
	PxTriangleMesh* tableMesh = cookMesh(verts, indices, 0.0f, 0, PxSdfBitsPerSubgridPixel::e16_BIT_PER_PIXEL);
	PxRigidStatic* table = gPhysics->createRigidStatic(PxTransform(PxIdentity));
	PxRigidActorExt::createExclusiveShape(*table, PxTriangleMeshGeometry(tableMesh), *gMaterial);
	scene->addActor(*table);
	tableMesh->release();

	tessellatedBoxMesh(PxVec3(0.0f), PxVec3(gCubeHalf), 24, verts, indices);
	PxTriangleMesh* mesh = cookMesh(verts, indices, 0.02f, 0, PxSdfBitsPerSubgridPixel::e32_BIT_PER_PIXEL);
	PxRigidDynamic* cube = gPhysics->createRigidDynamic(PxTransform(PxVec3(0.3f, gTableTop + gCubeHalf + 0.3f, -0.2f), PxQuat(0.2f, PxVec3(0.0f, 1.0f, 0.0f))));
	PxShape* shape = PxRigidActorExt::createExclusiveShape(*cube, PxTriangleMeshGeometry(mesh), *gMaterial);
	shape->setContactOffset(0.02f);
	shape->setRestOffset(0.0f);
	PxRigidBodyExt::updateMassAndInertia(*cube, 500.0f);
	cube->setSolverIterationCounts(16, 1);
	scene->addActor(*cube);
	mesh->release();

	simulate(scene, 240);
	position = cube->getGlobalPose().p;
	scene->release();
}

// ---------------------------------------------------------------------------
// GPU-only features.

// A block of PBD particles dropped onto a static SDF triangle-mesh table. Checks that no
// particle passes through the table and that none ends up higher than it started.
const PxReal gParticleStartLow = gTableTop + 0.5f;
const PxReal gParticleStartHigh = gParticleStartLow + 7 * 0.2f;

void particlesOnSdf(PxReal& minHeight, PxReal& meanHeight, PxReal& maxHeight, bool& finite)
{
	PxScene* scene = createScene(true);
	PxArray<PxVec3> verts;
	PxArray<PxU32> indices;
	boxMesh(PxVec3(0.0f, gTableTop - 0.5f, 0.0f), PxVec3(10.0f, 0.5f, 10.0f), verts, indices);
	PxTriangleMesh* tableMesh = cookMesh(verts, indices, 0.1f, 6, PxSdfBitsPerSubgridPixel::e16_BIT_PER_PIXEL);
	PxRigidStatic* table = gPhysics->createRigidStatic(PxTransform(PxIdentity));
	PxRigidActorExt::createExclusiveShape(*table, PxTriangleMeshGeometry(tableMesh), *gMaterial);
	scene->addActor(*table);
	tableMesh->release();

	const PxU32 n = 8;
	const PxU32 numParticles = n * n * n;
	const PxReal restOffset = 0.5f * 0.1f / 0.6f;
	// Start the particles farther apart than two solid rest offsets so they do not overlap.
	const PxReal spacing = 0.2f;
	PxPBDParticleSystem* particleSystem = gPhysics->createPBDParticleSystem(*gCudaContextManager, 96);
	particleSystem->setRestOffset(restOffset);
	particleSystem->setContactOffset(restOffset + 0.01f);
	particleSystem->setParticleContactOffset(1.5f * restOffset);
	particleSystem->setSolidRestOffset(restOffset);
	particleSystem->setFluidRestOffset(0.6f * restOffset);
	scene->addActor(*particleSystem);

	PxPBDMaterial* material = gPhysics->createPBDMaterial(0.2f, 0.05f, 0.0f, 0.001f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	const PxU32 phase = particleSystem->createPhase(material, PxParticlePhaseFlags(PxParticlePhaseFlag::eParticlePhaseSelfCollide));

	PxVec4* positions = PX_EXT_PINNED_MEMORY_ALLOC(PxVec4, *gCudaContextManager, numParticles);
	PxVec4* velocities = PX_EXT_PINNED_MEMORY_ALLOC(PxVec4, *gCudaContextManager, numParticles);
	PxU32* phases = PX_EXT_PINNED_MEMORY_ALLOC(PxU32, *gCudaContextManager, numParticles);
	for (PxU32 i = 0; i < numParticles; i++)
	{
		const PxU32 x = i % n, y = (i / n) % n, z = i / (n * n);
		positions[i] = PxVec4(PxReal(x) * spacing, gParticleStartLow + PxReal(y) * spacing, PxReal(z) * spacing, 1.0f);
		velocities[i] = PxVec4(0.0f);
		phases[i] = phase;
	}
	ExtGpu::PxParticleBufferDesc bufferDesc;
	bufferDesc.maxParticles = numParticles;
	bufferDesc.numActiveParticles = numParticles;
	bufferDesc.positions = positions;
	bufferDesc.velocities = velocities;
	bufferDesc.phases = phases;
	PxParticleBuffer* buffer = ExtGpu::PxCreateAndPopulateParticleBuffer(bufferDesc, gCudaContextManager);
	particleSystem->addParticleBuffer(buffer);

	simulate(scene, 180);

	copyDeviceToHost(positions, buffer->getPositionInvMasses(), numParticles * sizeof(PxVec4));
	minHeight = PX_MAX_F32;
	maxHeight = -PX_MAX_F32;
	meanHeight = 0.0f;
	finite = true;
	for (PxU32 i = 0; i < numParticles; i++)
	{
		const PxVec3 p = positions[i].getXYZ();
		finite = finite && isFinite(p);
		minHeight = PxMin(minHeight, p.y);
		maxHeight = PxMax(maxHeight, p.y);
		meanHeight += p.y / PxReal(numParticles);
	}

	PX_EXT_PINNED_MEMORY_FREE(*gCudaContextManager, positions);
	PX_EXT_PINNED_MEMORY_FREE(*gCudaContextManager, velocities);
	PX_EXT_PINNED_MEMORY_FREE(*gCudaContextManager, phases);
	particleSystem->removeParticleBuffer(buffer);
	buffer->release();
	scene->release();
}

PxDeformableVolume* addDeformableBox(PxScene* scene, const PxVec3& center, PxReal size, PxReal density)
{
	PxDeformableVolumeMaterial* material = gPhysics->createDeformableVolumeMaterial(2.0e5f, 0.3f, 0.5f);
	PxDeformableVolume* volume = PxDeformableVolumeExt::createDeformableVolumeBox(PxTransform(center), PxVec3(size), *material, *gCudaContextManager, size / 3.0f, density, 8);
	volume->setSolverIterationCounts(30);
	scene->addActor(*volume);
	return volume;
}

void deformableVertices(PxDeformableVolume* volume, PxArray<PxVec3>& vertices)
{
	const PxU32 n = volume->getCollisionMesh()->getNbVertices();
	PxArray<PxVec4> buffer(n);
	copyDeviceToHost(buffer.begin(), volume->getPositionInvMassBufferD(), n * sizeof(PxVec4));
	vertices.clear();
	for (PxU32 i = 0; i < n; i++)
		vertices.pushBack(buffer[i].getXYZ());
}

// A deformable cube dropped on the ground. Checks that it comes to rest on the ground,
// neither sinking in nor collapsing.
void deformableOnGround(PxReal& minHeight, PxReal& maxHeight, bool& finite)
{
	PxScene* scene = createScene(true);
	addGround(scene);
	PxDeformableVolume* volume = addDeformableBox(scene, PxVec3(0.0f, 1.0f, 0.0f), 1.0f, 100.0f);
	simulate(scene, 180);
	PxArray<PxVec3> vertices;
	deformableVertices(volume, vertices);
	minHeight = PX_MAX_F32;
	maxHeight = -PX_MAX_F32;
	for (PxU32 i = 0; i < vertices.size(); i++)
	{
		minHeight = PxMin(minHeight, vertices[i].y);
		maxHeight = PxMax(maxHeight, vertices[i].y);
	}
	finite = allFinite(vertices);
	volume->release();
	scene->release();
}

// A fixed-base articulation lever held horizontal by a joint drive. With a deformable
// cube resting on its arm, the contact impulses the deformable solver hands to the
// articulation must bend the lever further. Returns the arm's pitch angle.
PxReal leverAngle(bool withLoad, PxReal& loadHeight)
{
	PxScene* scene = createScene(true);
	addGround(scene);
	PxArticulationReducedCoordinate* articulation = gPhysics->createArticulationReducedCoordinate();
	articulation->setArticulationFlag(PxArticulationFlag::eFIX_BASE, true);
	articulation->setSolverIterationCounts(32, 1);
	PxArticulationLink* base = articulation->createLink(NULL, PxTransform(PxVec3(0.0f, 1.0f, 0.0f)));
	PxRigidActorExt::createExclusiveShape(*base, PxBoxGeometry(0.05f, 0.05f, 0.05f), *gMaterial);
	PxRigidBodyExt::updateMassAndInertia(*base, 100.0f);
	PxArticulationLink* arm = articulation->createLink(base, PxTransform(PxVec3(1.0f, 1.0f, 0.0f)));
	PxRigidActorExt::createExclusiveShape(*arm, PxBoxGeometry(1.0f, 0.05f, 0.5f), *gMaterial);
	PxRigidBodyExt::updateMassAndInertia(*arm, 100.0f);
	PxArticulationJointReducedCoordinate* joint = arm->getInboundJoint();
	joint->setJointType(PxArticulationJointType::eREVOLUTE);
	joint->setMotion(PxArticulationAxis::eTWIST, PxArticulationMotion::eFREE);
	joint->setParentPose(PxTransform(PxVec3(0.0f), PxQuat(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f))));
	joint->setChildPose(PxTransform(PxVec3(-1.0f, 0.0f, 0.0f), PxQuat(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f))));
	joint->setDriveParams(PxArticulationAxis::eTWIST, PxArticulationDrive(2000.0f, 200.0f, PxPerformanceEnvelope(PX_MAX_F32)));
	joint->setDriveTarget(PxArticulationAxis::eTWIST, 0.0f);
	scene->addArticulation(*articulation);

	PxDeformableVolume* volume = NULL;
	if (withLoad)
		volume = addDeformableBox(scene, PxVec3(1.5f, 1.4f, 0.0f), 0.5f, 200.0f);
	simulate(scene, 240);

	loadHeight = 0.0f;
	if (volume)
	{
		PxArray<PxVec3> vertices;
		deformableVertices(volume, vertices);
		for (PxU32 i = 0; i < vertices.size(); i++)
			loadHeight += vertices[i].y / PxReal(vertices.size());
		volume->release();
	}
	const PxQuat q = arm->getGlobalPose().q;
	const PxReal angle = 2.0f * PxAtan2(q.z, q.w);
	scene->release();
	return angle;
}

// Columns of convex-mesh boxes on the ground, simulated with a GPU contact buffer far too
// small for their contacts. The narrowphase must report the overflow rather than drop the
// contacts silently. Returns the number of overflow reports.
PxU32 contactOverflowReports()
{
	PxGpuDynamicsMemoryConfig config;
	config.maxRigidContactCount = 64;
	config.maxRigidPatchCount = 16;
	PxScene* scene = createScene(true, &config);
	addGround(scene);

	const PxVec3 corners[8] = {
		PxVec3(-0.25f, -0.25f, -0.25f), PxVec3(0.25f, -0.25f, -0.25f), PxVec3(-0.25f, 0.25f, -0.25f), PxVec3(0.25f, 0.25f, -0.25f),
		PxVec3(-0.25f, -0.25f, 0.25f), PxVec3(0.25f, -0.25f, 0.25f), PxVec3(-0.25f, 0.25f, 0.25f), PxVec3(0.25f, 0.25f, 0.25f) };
	PxConvexMeshDesc convexDesc;
	convexDesc.points.count = 8;
	convexDesc.points.stride = sizeof(PxVec3);
	convexDesc.points.data = corners;
	convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
	PxCookingParams params(gPhysics->getTolerancesScale());
	params.buildGPUData = true;
	PxConvexMesh* convex = PxCreateConvexMesh(params, convexDesc, gPhysics->getPhysicsInsertionCallback());

	for (PxU32 i = 0; i < 25; i++)
	{
		for (PxU32 level = 0; level < 4; level++)
		{
			const PxVec3 p(PxReal(i % 5) * 1.0f, 0.25f + PxReal(level) * 0.5f, PxReal(i / 5) * 1.0f);
			PxRigidDynamic* body = PxCreateDynamic(*gPhysics, PxTransform(p), PxConvexMeshGeometry(convex), *gMaterial, 1000.0f);
			scene->addActor(*body);
		}
	}
	convex->release();

	gErrorCallback.mOverflowReports = 0;
	gErrorCallback.mExpectOverflow = true;
	simulate(scene, 10);
	gErrorCallback.mExpectOverflow = false;
	scene->release();
	return gErrorCallback.mOverflowReports;
}

} // namespace

int snippetMain(int, const char*const*)
{
	gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
	gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale(), true);
	PxInitExtensions(*gPhysics, NULL);
	// A few worker threads are enough for these small scenes.
	gDispatcher = PxDefaultCpuDispatcherCreate(PxMin(SnippetUtils::getNbPhysicalCores(), 4u));
	gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.0f);

	PxCudaContextManagerDesc cudaContextManagerDesc;
	gCudaContextManager = PxCreateCudaContextManager(*gFoundation, cudaContextManagerDesc, PxGetProfilerCallback());
	if (!gCudaContextManager || !gCudaContextManager->contextIsValid())
	{
		check(false, "setup", "GPU context created", 0.0f, 1.0f);
	}
	else
	{
		printf("GPU: %s\n", gCudaContextManager->getDeviceName());

		PxArray<PxVec3> pc, pg, ac, ag;
		freeFall(false, pc, ac);
		freeFall(true, pg, ag);
		check(maxDistance(pc, pg) < 1e-3f, "free fall", "GPU vs CPU max position difference (m)", maxDistance(pc, pg), 1e-3f);
		check(maxDistance(ac, ag) < 1e-3f, "free fall", "GPU vs CPU max axis difference", maxDistance(ac, ag), 1e-3f);

		PxReal topC, topG, speedC, speedG;
		pc.clear(); pg.clear();
		stacks(false, pc, topC, speedC);
		stacks(true, pg, topG, speedG);
		check(allFinite(pg), "stacks", "GPU positions finite", 0.0f, 0.0f);
		check(maxDistance(pc, pg) < 0.01f, "stacks", "GPU vs CPU max position difference (m)", maxDistance(pc, pg), 0.01f);
		check(PxAbs(topG - 2.75f) < 0.01f, "stacks", "GPU tower top height error (m)", PxAbs(topG - 2.75f), 0.01f);
		check(speedG < 0.05f, "stacks", "GPU max speed after settling (m/s)", speedG, 0.05f);

		pc.clear(); pg.clear();
		articulationContacts(false, pc);
		articulationContacts(true, pg);
		check(allFinite(pg), "articulation", "GPU positions finite", 0.0f, 0.0f);
		check(maxDistance(pc, pg) < 0.01f, "articulation", "GPU vs CPU max position difference (m)", maxDistance(pc, pg), 0.01f);

		pc.clear(); pg.clear();
		sdfMeshes(false, pc);
		sdfMeshes(true, pg);
		const char* formats[4] = { "dense", "sparse 8-bit", "sparse 16-bit", "sparse 32-bit" };
		for (PxU32 i = 0; i < pg.size() && i < pc.size(); i++)
		{
			char what[64];
			snprintf(what, sizeof(what), "%s: GPU vs CPU position difference (m)", formats[i]);
			check((pg[i] - pc[i]).magnitude() < 0.005f, "SDF mesh", what, (pg[i] - pc[i]).magnitude(), 0.005f);
			snprintf(what, sizeof(what), "%s: GPU rest height error (m)", formats[i]);
			// A quarter of the 0.02 m SDF spacing.
			const PxReal err = PxAbs(pg[i].y - (gTableTop + gCubeHalf));
			check(err < 0.005f, "SDF mesh", what, err, 0.005f);
		}

		PxVec3 largeC, largeG;
		largeSdfMesh(false, largeC);
		largeSdfMesh(true, largeG);
		check((largeG - largeC).magnitude() < 0.005f, "large SDF mesh", "GPU vs CPU position difference (m)", (largeG - largeC).magnitude(), 0.005f);
		check(PxAbs(largeG.y - (gTableTop + gCubeHalf)) < 0.005f, "large SDF mesh", "GPU rest height error (m)", PxAbs(largeG.y - (gTableTop + gCubeHalf)), 0.005f);

		PxReal minH, meanH, maxH;
		bool finite;
		particlesOnSdf(minH, meanH, maxH, finite);
		const PxReal startMean = 0.5f * (gParticleStartLow + gParticleStartHigh);
		check(finite, "particles on SDF", "positions finite", 0.0f, 0.0f);
		check(minH > gTableTop - 0.02f, "particles on SDF", "lowest particle height (m)", minH, gTableTop - 0.02f);
		check(maxH < gParticleStartHigh, "particles on SDF", "highest particle height (m), below start", maxH, gParticleStartHigh);
		check(meanH < startMean - 0.2f, "particles on SDF", "mean particle height (m), fell onto table", meanH, startMean - 0.2f);

		deformableOnGround(minH, maxH, finite);
		check(finite, "deformable", "vertices finite", 0.0f, 0.0f);
		check(minH > -0.02f, "deformable", "lowest vertex height (m)", minH, -0.02f);
		check(maxH > 0.8f && maxH < 1.1f, "deformable", "highest vertex height (m), in [0.8, 1.1]", maxH, 1.1f);

		PxReal loadHeight, unused;
		const PxReal unloaded = leverAngle(false, unused);
		const PxReal loaded = leverAngle(true, loadHeight);
		// Static estimate of the extra bend: 25 kg at about 1.5 m on a 2000 Nm/rad drive is 0.18 rad.
		const PxReal bend = unloaded - loaded;
		check(bend > 0.06f && bend < 0.5f, "articulation lever", "extra bend under deformable load (rad)", bend, 0.06f);
		check(loadHeight > 0.8f, "articulation lever", "deformable load stays on the arm (m)", loadHeight, 0.8f);

		const PxU32 overflows = contactOverflowReports();
		check(overflows > 0, "contact overflow", "buffer overflow reports", PxReal(overflows), 1.0f);
	}
	check(gErrorCallback.mErrors == 0, "all", "PhysX errors reported", PxReal(gErrorCallback.mErrors), 0.0f);
	printf("%u checks, %u failed\n", gChecks, gFailures);

	PX_RELEASE(gDispatcher);
	PxCloseExtensions();
	PX_RELEASE(gPhysics);
	PX_RELEASE(gCudaContextManager);
	PX_RELEASE(gFoundation);
	printf("SnippetGpuParity done.\n");
	return gFailures ? 1 : 0;
}
