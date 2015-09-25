
#include "iostream"
#include "PxPhysicsAPI.h"

#include "atMap.h++"

#include "PhysXCollisionCallback.h++"
#include "PhysXJoint.h++"
#include "PhysXRigidActor.h++"


#ifdef _WIN32
#define PHYSX_API extern "C" __declspec(dllexport)
#else
#define PHYSX_API extern "C"
#endif

using namespace std;
using namespace physx;


static PxFoundation *                    px_foundation;
static PxPhysics *                       px_physics;
static PxScene *                         px_scene;
static PxCooking *                       px_cooking;

static PxDefaultErrorCallback            error_callback;
static PxDefaultAllocator                allocator_callback;
static PhysXCollisionCallback *          px_collisions;

static PxRigidStatic *                   ground_plane;

static int                               scene_initialized = 0;

static int                               max_updates;

static atMap *                           actor_map = new atMap();
static atMap *                           joint_map = new atMap();

static debugger::comm::PvdConnection *   theConnection = NULL;

static atInt *                           terrain_id = new atInt(1234);

static float                             height_field_scale;


struct EntityProperties
{
   unsigned int   ID;
   const char *   Name;
   float          PositionX;
   float          PositionY;
   float          PositionZ;
   float          RotationX;
   float          RotationY;
   float          RotationZ;
   float          RotationW;
   float          VelocityX;
   float          VelocityY;
   float          VelocityZ;
   float          AngularVelocityX;
   float          AngularVelocityY;
   float          AngularVelocityZ;
};

static EntityProperties * update_array;
static CollisionProperties * collisions_array;


//-----------------------------------------------------------------------------


PhysXRigidActor * createActor(unsigned int id,
   char * name, float x, float y, float z, bool isDynamic)
{
   PhysXRigidActor *   actor;
   ActorType           actorType;

   // Determine whether the actor to be created is dynamic or static
   if (isDynamic)
   {
      actorType = DYNAMIC;
   }
   else
   {
      actorType = STATIC;
   }

   // Create a new rigid actor with the given position and actor type
   actor = new PhysXRigidActor(px_physics, id, x, y, z, actorType);
   actor->setName(name);

   // Keep track of the actor in the map and then return it
   actor_map->addEntry(actor->getID(), actor);
   return actor;
}


PhysXRigidActor * createActor(unsigned int id,
   char * name, float x, float y, float z, PxQuat Rot, bool isDynamic)
{
   PhysXRigidActor *   actor;
   ActorType           actorType;

   // Determine whether the actor to be created is dynamic or static
   if (isDynamic)
   {
      actorType = DYNAMIC;
   }
   else
   {
      actorType = STATIC;
   }

   // Create a new rigid actor with the given position and actor type
   actor = new PhysXRigidActor(px_physics, id, x, y, z, Rot, actorType);
   actor->setName(name);

   // Keep track of the actor in the map and then return it
   actor_map->addEntry(actor->getID(), actor);
   return actor;
}


PhysXRigidActor * getActor(unsigned int id)
{
   PhysXRigidActor *   rigidActor;

   // Find the actor, with the specified ID, in the map, and then
   // return it if found; otherwise, return NULL
   rigidActor = (PhysXRigidActor *)actor_map->getValue(new atInt(id));
   return rigidActor;
}


PhysXRigidActor * getActor(atInt * id)
{
   PhysXRigidActor *   rigidActor;

   // Find the actor, with the spcified id, in the map, and then
   // return it if found; otherwise, return NULL
   rigidActor = (PhysXRigidActor *)actor_map->getValue(id);
   return rigidActor;
}


// Custom filter shader used for collision filtering and to customize the
// collection of flags describing the actions to take on a collision pair
PxFilterFlags contactFilterShader(PxFilterObjectAttributes attributes0,
   PxFilterData filterData0, PxFilterObjectAttributes attributes1,
   PxFilterData filterData1, PxPairFlags& pairFlags,
   const void * constantBlock, PxU32 constantBlockSize)
{
   // Check to see if either actor is a trigger
   if (PxFilterObjectIsTrigger(attributes0) ||
       PxFilterObjectIsTrigger(attributes1))
   {
      // Signal that a trigger has been activated and exit
      pairFlags = PxPairFlag::eCONTACT_DEFAULT | PxPairFlag::eTRIGGER_DEFAULT |
         PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
         PxPairFlag::eNOTIFY_CONTACT_POINTS;

      return PxFilterFlag::eDEFAULT;
   }

   // Generate a default contact report
   pairFlags |= PxPairFlag::eCONTACT_DEFAULT;

   // Always report collision
   pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
   pairFlags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;

   // Add the Continuous Collision Detection (CCD) flag, so that
   // CCD is enabled, and return the default filter flags
   pairFlags |= PxPairFlag::eCCD_LINEAR;
   return PxFilterFlag::eDEFAULT;
}


void startVisualDebugger()
{
   // Check that the visual debugger is able to run
   if (px_physics->getPvdConnectionManager() == NULL)
   {
      // The visual debugger won't work so let the user know and stop
      // trying to create a connection
      printf("\n\nUnable to start the visual debugger.\n\n");
      return;
   }

   // Create the ip address, port, and timeout for the connection
   const char * pvd_host_ip = "10.171.195.129";
   int port = 5425;
   unsigned int timeout = 100;

   // Set the flags that you wish for the visual debugger to have access to
   // the different values, specifically profile, debug, and memory
   PxVisualDebuggerConnectionFlags connectionFlags =
      PxVisualDebuggerExt::getAllConnectionFlags();

   // Establish the connection with nvidia visual debugger
   theConnection = PxVisualDebuggerExt::createConnection(
      px_physics->getPvdConnectionManager(), pvd_host_ip, port, timeout,
      connectionFlags);

   // Let the user know that the connection to the PhysX Visual Debugger could
   // not be created
   if (theConnection)
   {
      printf("\n\nConnected to PhysX Visual Debugger!\n\n");
   }
}


//-----------------------------------------------------------------------------


PHYSX_API int initialize()
{
   // Create and initialize the PhysX foundation
   px_foundation = PxCreateFoundation(
      PX_PHYSICS_VERSION, allocator_callback, error_callback);

   // Create the top-level physics object
   px_physics = PxCreatePhysics(
      PX_PHYSICS_VERSION, *px_foundation, PxTolerancesScale());

   // Return 0 (false) if the physics object could not be created
   if (px_physics == NULL)
      return 0;

   // Create a cooking object that will generate meshes
   px_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *px_foundation,
      PxCookingParams(PxTolerancesScale()));

   // For now us a default value for the height scale to change the height
   // field values from floats to integers and back again
   height_field_scale = 0.001f;

   // Warn user that cooking utilities were unable to be created
   if (px_cooking == NULL)
   {
      cerr << "Cooking utilities failed to initialize!\n";
   }

   // Create the collision callback
   px_collisions = new PhysXCollisionCallback();

   // Initialize the visual debugger
   startVisualDebugger();

   // Successfully initialized PhysX
   return 1;
}


PHYSX_API void release()
{
   // Close the visual debugger if it's currently running
   if (theConnection)
   {
        theConnection->release();
   }

   // Shut down the physics entirely
   px_physics->release();
   px_foundation->release();
}


PHYSX_API void initEntityUpdate(EntityProperties * updateArray, int maxUpdates)
{
   // Keep reference to the given array for entity property updates and
   // the max number of updates allowed
   update_array = updateArray;
   max_updates = maxUpdates;
}


PHYSX_API void initCollisionUpdate(
   CollisionProperties * collisionArray, int maxCollisions)
{
   // Set the collisions array to the pointer given
   collisions_array = collisionArray;

   if (px_collisions)
   {
       px_collisions->setCollisionsArray(collisions_array, maxCollisions);
   }
}


PHYSX_API int createScene(bool gpuEnabled, bool cpuEnabled, int cpuMaxThreads)
{
   PxDefaultCpuDispatcher *   cpuDispatcher;
   PxCudaContextManagerDesc   cudaManagerDesc;
   PxCudaContextManager *     cudaContextManager;
   PxProfileZoneManager *     profileZoneManager;

   // TODO: the gravity sets the normals for the scene: currently
   // set the gravity to the z-axis because of OpenSim; this should
   // be given instead

   // Create the descriptor class for the scene with the default tolerance
   // parameters for the simulation and real-world gravity
   PxSceneDesc sceneDesc = PxSceneDesc(px_physics->getTolerancesScale());
   sceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.8f);

   // Set flags to configure the properties for the scene; will enable:
   // active transform notifications,
   // continuous collosion detection,
   // contact pair filtering between kinematic rigid bodies,
   // contact pair filtering between kinematic and static rigid bodies
   sceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVETRANSFORMS;
   sceneDesc.flags |= PxSceneFlag::eENABLE_CCD;
   sceneDesc.flags |= PxSceneFlag::eENABLE_KINEMATIC_PAIRS;

   // Check if the gpu dispatcher is enabled
   if (gpuEnabled)
   {
      // Print errors and debug for this function split from the rest of the
      // errors and debug
      printf("\n[PhysXLib::createScene]\n");

      #if PX_SUPPORT_GPU_PHYSX
      printf("GPU is supported.\n");
      #endif

      #if !defined(RENDERER_PVD) && !defined(PX_XBOXONE)
      printf("Renderer enabled cuda.\n");
      #endif

      // TODO: Check which graphics library is being used
      if (1)
      {
         // Let the user know which type of device is being used
         printf("GPU interop and device set for OpenGL.\n");

         // Use OpenGL
         cudaManagerDesc.interopMode = PxCudaInteropMode::OGL_INTEROP;

         // Need a Cg context for the device
         cudaManagerDesc.graphicsDevice = (void *) NULL;
      }

      // Create the profiler zone manager for the cuda context to send profile
      // updates
      profileZoneManager = &PxProfileZoneManager::createProfileZoneManager(
         px_foundation);

      if (!profileZoneManager)
      {
         printf("Profile Zone Manager failed.\n");
      }

      // Create the cuda context manager that has the gpu dispatcher that will
      // be used with the scene
      cudaContextManager = PxCreateCudaContextManager(*px_foundation,
         cudaManagerDesc, profileZoneManager);

      // Check that the cuda context was created succesfully, has a valid
      // context, and that the scene description doesn't have a gpu dispatcher
      if (cudaContextManager && cudaContextManager->contextIsValid() &&
         sceneDesc.gpuDispatcher == NULL)
      {
         // Since cuda was correctly created and the scene doesn't have a gpu
         // dispatcher get the cuda gpu dispatcher
         sceneDesc.gpuDispatcher = cudaContextManager->getGpuDispatcher();

         // Let the user know that the GPU is being used
         printf("GPU enabled.\n");
      }
      else
      {
         // Let the user know that the GPU was not created succesfully
         printf("GPU failed to initialize.\n");

         if (!cudaContextManager)
         {
            printf("Context Manager NULL.\n");
         }
         else if (!cudaContextManager->contextIsValid())
         {
            printf("Context not valid.\n");
         }
         else
         {
            printf("Scene had a GPU dispatcher.\n");
         }

         // Disable the gpu in order to try and save PhysXWrapper by enabling
         // the CPU as a backup
         gpuEnabled = false;
      }
   }

   // This check will enable the CPU if the user wanted it enabled, but it will
   // also recover from a GPU failure and initialise the CPU
   if (!gpuEnabled || cpuEnabled)
   {
      // The CPU dispatcher is needed for interfacing with the application's
      // thread pool; check if the scene description already has one
      if (sceneDesc.cpuDispatcher == NULL)
      {
         // Confirm that the CPU was supposed to be used
         if (!cpuEnabled)
         {
            // Since the CPU was not originally going to be in use assign 0 as
            // the thread count to force the program to use the same thread as
            // the wrapper
            cpuMaxThreads = 0;
         }

         // No dispatcher found so create a new one with one worker thread to
         // start
         cpuDispatcher = PxDefaultCpuDispatcherCreate(cpuMaxThreads);

         // Return 0 (false) if CPU dispatcher failed to create
         if (cpuDispatcher == NULL && !gpuEnabled)
         {
            return 0;
         }
         else if (cpuDispatcher != NULL)
         {
            // Notify the user that the CPU is currently in use
            printf("CPU enabled.\n");

            // Assign the created dispatcher to the scene description
            sceneDesc.cpuDispatcher = cpuDispatcher;
         }
      }
   }

   // Use a custom filter shader, which enables CCD to work
   sceneDesc.filterShader = contactFilterShader;

   // Create the physics scene
   px_scene = px_physics->createScene(sceneDesc);

   // Print error and return 0 (false) if the scene failed to be created
   if (px_scene == NULL)
   {
      cerr << "Failed to create scene!\n";
      return 0;
   }

   // Set the custom collisions callback to receive simulation
   // events related to collisions
   px_scene->setSimulationEventCallback(px_collisions);

   // Separate this section inside of the debug and errors
   printf("[PhysXLib::createScene]\n\n");

   // Successfully created the scene
   scene_initialized = 1;
   return 1;
}


PHYSX_API void releaseScene()
{
   // Release all objects in the scene
   px_scene->release();
}


PHYSX_API void createActorSphere(
   unsigned int id, char * name, float x, float y, float z,
   float staticFriction, float dynamicFriction, float restitution,
   float radius, float density, bool isDynamic)
{
   PhysXRigidActor *    actor;
   PxMaterial *         material;
   PxSphereGeometry     geometry;
   PxShape *            shape;

   // Check that the scene has been initialized
   if (scene_initialized == 1)
   {
      px_scene->lockWrite();

      // Create the rigid actor and add it to the scene
      actor = createActor(id, name, x, y, z, isDynamic);

      // Create a new material; used to resolve collisions
      material = px_physics->createMaterial(
         staticFriction, dynamicFriction, restitution);

      // Create a sphere geometry and use it and the material
      // to create a new shape
      geometry = PxSphereGeometry(radius);
      shape = px_physics->createShape(geometry, *material);

      // Assign the new shape to the actor
      actor->setShape(shape);

      // Add the newly created actor to the scene
      px_scene->addActor(*(actor->getActor()));

      px_scene->unlockWrite();
   }
   else
   {
      // Alert that the actor could not be created
      cerr << "Failed to create actor! Scene has not been initialized.\n";
   }
}


PHYSX_API void createActorBox(
   unsigned int id, char * name, float posX, float posY, float posZ,
   float staticFriction, float dynamicFriction, float restitution,
   float halfX, float halfY, float halfZ, float density, bool isDynamic)
{
   PhysXRigidActor *   actor;
   PxMaterial *        material;
   PxBoxGeometry       geometry;
   PxShape *           shape;

   // Check that the scene has been initialized
   if (scene_initialized == 1)
   {
      px_scene->lockWrite();

      // Create the rigid actor and add it to the scene
      actor = createActor(id, name, posX, posY, posZ, isDynamic);

      // Create a new material; used to resolve collisions
      material = px_physics->createMaterial(
         staticFriction, dynamicFriction, restitution);

      // Create a box geometry and use it and the material
      // to create a new shape
      geometry = PxBoxGeometry(halfX, halfY, halfZ);
      shape = px_physics->createShape(geometry, *material);

      // Assign the new shape to the actor
      actor->setShape(shape);

      // Add the newly created actor to the scene
      px_scene->addActor(*(actor->getActor()));

      px_scene->unlockWrite();
   }
   else
   {
      // Alert that the actor could not be created
      cerr << "Failed to create actor! Scene has not been initialized.\n";
   }
}


PHYSX_API void createActorCapsule(
   unsigned int id, char * name, float x, float y, float z, float rotX,
   float rotY, float rotZ, float rotW, float staticFriction,
   float dynamicFriction, float restitution, float halfHeight, float radius,
   float density, bool isDynamic)
{
   PhysXRigidActor *     actor;
   PxMaterial *          material;
   PxCapsuleGeometry     geometry;
   PxShape *             shape;
   PxTransform           relativePose;

   // Check that the scene has been initialized
   if (scene_initialized == 1)
   {
      px_scene->lockWrite();

      // Create the rigid actor and add it to the scene
      actor = createActor(id, name, x, y, z, isDynamic);

      // Create a new material; used to resolve collisions
      material = px_physics->createMaterial(
         staticFriction, dynamicFriction, restitution);

      // Create a capsule geometry and use it and the material
      // to create a new shape
      geometry = PxCapsuleGeometry(halfHeight, radius);
      shape = px_physics->createShape(geometry, *material);

      // TODO: Relative transform should be given
      // Create a relative transform for the capsule geometry to stand
      // upright; rotate transform around the Z-axis by a quater-circle
      relativePose = PxTransform(PxQuat(PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f)));
      shape->setLocalPose(relativePose);

      // Assign the new shape to the actor
      actor->setShape(shape);

      // Add the newly created actor to the scene
      px_scene->addActor(*(actor->getActor()));

      px_scene->unlockWrite();
   }
   else
   {
      // Alert that the actor could not be created
      cerr << "Failed to create actor! Scene has not been initialized.\n";
   }
}


PHYSX_API void createActorTriangleMesh(
   unsigned int id, char * name, float x, float y, float z,
   float staticFriction, float dynamicFriction, float restitution,
   float* vertices, int* indices, int vertexCount, int indexCount,
   bool isDynamic)
{
   PhysXRigidActor *        actor;
   PxMaterial *             material;
   PxShape *                meshShape;
   PxVec3 *                 vertexArray;
   PxU32 *                  indexArray;
   PxTriangleMesh *         triangleMesh;
   PxTriangleMeshGeometry   meshGeom;
   PxTriangleMeshDesc       meshDesc;
   PxMeshScale              meshScale;

   // Don't create a new actor if the scene hasn't been initialized yet
   if (scene_initialized == 0)
   {
        return;
   }

   // Prevent scene from being written to while actor is being created
   px_scene->lockWrite();

   // Create the rigid actor for this mesh and add it to the scene
   actor = createActor(id, name, x, y, z, isDynamic);

   // Create a new material; used to resolve collisions
   material =
      px_physics->createMaterial(staticFriction, dynamicFriction, restitution);

   // Convert the given array of vertex points to an array of PhysX vectors,
   // for use by PhysX
   vertexArray = new PxVec3[vertexCount];
   for (int i = 0; i < vertexCount; i++)
   {
      vertexArray[i] =
         PxVec3(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
   }

   // Convert the given array of indices into an array of PhysX unsigned
   // integers
   indexArray = new PxU32[indexCount];
   for (int i = 0; i < indexCount; i++)
   {
        indexArray[i] = (PxU32) indices[i];
   }

   // Constuct a description of the actor mesh
   meshDesc.points.count = vertexCount;
   meshDesc.points.stride = sizeof(PxVec3);
   meshDesc.points.data = vertexArray;
   meshDesc.triangles.count = indexCount / 3;
   meshDesc.triangles.stride = sizeof(PxU32) * 3;
   meshDesc.triangles.data = indexArray;

   // Create the triangle mesh using the cooking library
   triangleMesh = px_cooking->createTriangleMesh(
      meshDesc, px_physics->getPhysicsInsertionCallback());

   // Create a geometry
   meshScale.scale = PxVec3(1.0f, 1.0f, 1.0f);
   meshScale.rotation = PxQuat::createIdentity();
   meshGeom = PxTriangleMeshGeometry(
      triangleMesh, meshScale, PxMeshGeometryFlag::eDOUBLE_SIDED);

   // Create a new shape for the mesh and add it to the actor
   meshShape = px_physics->createShape(meshGeom, *material);
   actor->setShape(meshShape);

   // Add the newly created actor to the scene
   px_scene->addActor(*(actor->getActor()));

   // Finished creating new mesh actor
   px_scene->unlockWrite();
}


PHYSX_API void createActorConvexMesh(
   unsigned int id, char * name, float x, float y, float z,
   float staticFriction, float dynamicFriction, float restitution,
   float* vertices, int vertexCount, bool isDynamic)
{
   PhysXRigidActor *      actor;
   PxMaterial *           material;
   PxShape *              meshShape;
   PxVec3 *               vertexArray;
   PxConvexMesh *         convexMesh;
   PxConvexMeshGeometry   meshGeom;
   PxConvexMeshDesc       meshDesc;
   PxMeshScale            meshScale;
   PxDefaultMemoryOutputStream   buffer;
   PxDefaultMemoryInputData*      inputData;

   // Don't create a new actor if the scene hasn't been initialized yet
   if (scene_initialized == 0)
   {
        return;
   }

   // Prevent scene from being written to while actor is being created
   px_scene->lockWrite();

   // Create the rigid actor for this mesh and add it to the scene
   actor = createActor(id, name, x, y, z, isDynamic);

   // Create a new material; used to resolve collisions
   material =
      px_physics->createMaterial(staticFriction, dynamicFriction, restitution);

   // Convert the given array of vertex points to an array of PhysX
   // vectors, for use by PhysX
   vertexArray = new PxVec3[vertexCount];
   for (int i = 0; i < vertexCount; i++)
   {
      vertexArray[i] =
         PxVec3(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
   }

   // Construct a description of the actor mesh
   meshDesc.points.count = vertexCount;
   meshDesc.points.stride = sizeof(PxVec3);
   meshDesc.points.data = vertexArray;
   meshDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
   meshDesc.vertexLimit = 256;

   // Attempt to 'cook' the mesh data into a form which allows PhysX to
   // perform efficient collision detection; results are written to
   // the stream buffer
   if (px_cooking->cookConvexMesh(meshDesc, buffer))
   {
      // Sucessfully cooked the convex mesh so create the input stream
      // from the resulting data and use it to create the convex mesh
      inputData =
         new PxDefaultMemoryInputData(buffer.getData(), buffer.getSize());
      convexMesh = px_physics->createConvexMesh(*inputData);

      // Create the geometry for the mesh
      meshScale.scale = PxVec3(1.0f, 1.0f, 1.0f);
      meshScale.rotation = PxQuat::createIdentity();
      meshGeom = PxConvexMeshGeometry(convexMesh, meshScale);

      // Create a new shape for the mesh and add it to the associated actor
      meshShape = px_physics->createShape(meshGeom, *material);
      actor->setShape(meshShape);

      // Add the newly created actor to the scene
      px_scene->addActor(*(actor->getActor()));
   }

   // Finished creating new mesh actor
   px_scene->unlockWrite();
}


PHYSX_API void removeActor(unsigned int id)
{
   PhysXRigidActor *           rigidActor;
   PxActor *                   actor;

   // Can't remove actor if scene has not been initialized yet
   if (scene_initialized == 0)i
   {
        return;
   }

   // Try and remove the given actor from the map and check to see
   // if the actor exists
   rigidActor = (PhysXRigidActor *)actor_map->removeEntry(new atInt(id));
   if (rigidActor == NULL)
   {
      // Alert that the given actor name could not be found
      cerr << "Failed to remove actor '" << id << "'. Actor not found.\n";
      return;
   }

   px_scene->lockWrite();

   // Remove the desired actor from the scene and specify that all
   // touching objects should be updated (woken up)
   actor = rigidActor->getActor();
   px_scene->removeActor(*actor, true);

   // Finalize removal by removing the actor data
   delete rigidActor;

   px_scene->unlockWrite();
}


PHYSX_API bool updateActorDensity(unsigned int id, float density)
{
   PhysXRigidActor * rigidActor;

   // Fetch the actor by the given id
   rigidActor = getActor(id);

   // Update the density for the actor if it is dynamic
   if (rigidActor != NULL && rigidActor->isDynamic())
   {
      return rigidActor->setDensity(density);
   }

   // If the actor is not dynamic, or is not found, return false
   return false;
}


PHYSX_API bool updateActorMass(unsigned int id, float mass)
{
   PhysXRigidActor * rigidActor;

   // Fetch the actor by the given id
   rigidActor = getActor(id);

   // Update the mass for the actor if it is dynamic
   if (rigidActor != NULL && rigidActor->isDynamic())
   {
      return rigidActor->setMass(mass);
   }

   // If the actor is not dynamic, or is not found, return false
   return false;
}


PHYSX_API float getActorMass(unsigned int id)
{
   PhysXRigidActor * rigidActor;

   // Fetch the physx actor by the given id
   rigidActor = getActor(id);

   // If the actor is not null, and is dynamic
   // Return the mass
   if (rigidActor != NULL && rigidActor->isDynamic())
   {
      return rigidActor->getMass();
   }

   // Otherwise, return 0.0f
   return 0.0f;
}

PHYSX_API bool addForce(unsigned int id, float forceX, float forceY, float forceZ)
{
   PxVec3 force;
   PhysXRigidActor * rigidActor;

   // Create the force vector and get the actor
   force = PxVec3(forceX, forceY, forceZ);
   rigidActor = getActor(id);

   // If the actor is not null, apply the force
   if (rigidActor != NULL)
   {
      return rigidActor->addForce(force);
   }

   return false;
}


PHYSX_API void setTranslation(unsigned int id, float posX, float posY,
   float posZ, float rotX, float rotY, float rotZ, float rotW)
{
   PhysXRigidActor *   rigidActor;

   // Attempt to get the specified actor by its id
   rigidActor = getActor(id);

   // If we have succesfully retrieved the actor
   // set the translation
   if (rigidActor != NULL)
   {
      px_scene->lockWrite();
      rigidActor->setTranslation(posX, posY, posZ, rotX, rotY, rotZ, rotW);
      px_scene->unlockWrite();
   }
}


PHYSX_API void setPosition(unsigned int id, float x, float y, float z)
{
   PhysXRigidActor *   rigidActor;

   // Update the position of the given actor, if found
   rigidActor = getActor(id);
   if (rigidActor != NULL)
   {
      px_scene->lockWrite();
      rigidActor->setPosition(x, y, z);
      px_scene->unlockWrite();
   }
   else
   {
      cerr << "Failed to update actor's '" << id << "' position."
         " Actor not found.\n";
   }
}


PHYSX_API float * getPosition(unsigned int id)
{
   PhysXRigidActor *   rigidActor;

   // Get the actor associated with the identifier from the map of actors
   rigidActor = getActor(id);

   // Make sure the actor was found
   if (rigidActor != NULL)
   {
      // Return the current position of this actor
      return rigidActor->getPosition();
   }
   else
   {
      // Let the user know that the actor could not be found
      cerr << "Failed to retrieve actor's '" << id << "' position.\n"
         " Actor not found.";
      return 0;
   }
}


PHYSX_API void setRotation(unsigned int id, float x, float y, float z, float w)
{
   PhysXRigidActor *   rigidActor;

   // Get the actor associated with the identifier from the map of actors
   rigidActor = getActor(id);

   // Make sure the actor was found
   if (rigidActor != NULL)
   {
      // Update the orientation of the actor
      px_scene->lockWrite();
      rigidActor->setRotation(x, y, z, w);
      px_scene->unlockWrite();
   }
   else
   {
      // Let the user know that the actor could not be found
      cerr << "Failed to update actor's '" << id << "' rotation."
         " Actor not found.\n";
   }
}


PHYSX_API float * getRotation(unsigned int id)
{
   PhysXRigidActor *   rigidActor;

   // Get the actor associated with the identifier from the map of actors
   rigidActor = getActor(id);

   // Make sure the actor was found
   if (rigidActor != NULL)
   {
      // Return the current orientation of the actor
      return rigidActor->getRotation();
   }
   else
   {
      // Let the user know that the actor could not be found
      cerr << "Failed to retrieve actor's '" << id << "' rotation."
         " Actor not found.\n";
      return 0;
   }
}


PHYSX_API void setLinearVelocity(unsigned int id, float x, float y, float z)
{
   PhysXRigidActor *   rigidActor;

   // Get the actor associated with the identifier from the map of actors
   rigidActor = getActor(id);

   // Make sure the actor was found
   if (rigidActor != NULL)
   {
      // Update the linear velocity of the actor
      px_scene->lockWrite();
      rigidActor->setLinearVelocity(x, y, z);
      px_scene->unlockWrite();
   }
}


PHYSX_API void setAngularVelocity(unsigned int id, float x, float y, float z)
{
   PhysXRigidActor *   rigidActor;

   // Get the actor associated with the identifier from the map of actors
   rigidActor = getActor(id);

   // Make sure the actor was found
   if (rigidActor != NULL)
   {
      // Update the angular velocity of the actor
      rigidActor->setAngularVelocity(x, y, z);
   }
}


PHYSX_API void setGravity(unsigned int id, float x, float y, float z)
{
   PhysXRigidActor *   rigidActor;

   // Get the actor associated with the identifier from the map of actors
   rigidActor = getActor(id);

   // Make sure the actor was found
   if (rigidActor != NULL)
   {
      // Update the gravity to the new values
      rigidActor->setGravity(x, y, z);
   }
   else
   {
      // Failed to find the actor with the given identifier
      cerr << "Failed to update actor's '" << id << "' gravity."
         " Actor not found.\n";
   }
}


PHYSX_API void enableGravity(unsigned int id, bool enabled)
{
   PhysXRigidActor *   rigidActor;

   // Get the actor associated with the identifier from the map of actors
   rigidActor = getActor(id);

   // Make sure the actor was found
   if (rigidActor != NULL)
   {
      // Update the gravity of the actor
      px_scene->lockWrite();
      rigidActor->enableGravity(enabled);
      px_scene->unlockWrite();
   }
}


PHYSX_API void createGroundPlane(float x, float y, float z)
{
   PxTransform    planePos;
   PxMaterial *   material;

   // Create the position for the plane and rotate it along the z-axis
   // by 90 degrees (used in second parameter)
   planePos = PxTransform(PxVec3(x, y, z),
      PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f)));

   // TODO: Pass material
   // Create a default material (static friction, dynamic friction,
   // and restituion) for the ground plane
   material = px_physics->createMaterial(0.5, 0.5, 0.5);

   // TODO: The normals need to be passed into this method
   // Create a rigid static actor to represent the terrain and create
   // the plane geometry to define its shape
   ground_plane = PxCreatePlane(*px_physics, PxPlane(PxVec3(0,0,1),0),
      *material);

   // Add the plane to the scene
   px_scene->lockWrite();
   px_scene->addActor(*ground_plane);
   px_scene->unlockWrite();
}


PHYSX_API void releaseGroundPlane()
{
   // Remove the ground plane from the scene
   px_scene->lockWrite();
   px_scene->removeActor(*ground_plane);
   ground_plane = NULL;
   px_scene->unlockWrite();
}


PHYSX_API void setHeightField(int terrainShapeID, int regionSizeX,
   int regionSizeY, float rowSpacing, float columnSpacing, float * posts)
{
   PxHeightFieldDesc heightFieldDescription;
   uint64_t numPosts;
   PxHeightFieldSample * heightFieldSampleArray;
   PxStridedData stridedData;
   PxHeightField * heightField;
   PxHeightFieldGeometry * heightFieldGeometry;
   PxMaterial * physxMaterial;
   PxShape * newShape;
   PhysXRigidActor * actor;

   // TODO: Check that the terrain is added to the correct scene

   // Make sure the scene has been initialized
   if (scene_initialized != 1)
   {
      return;
   }

   // In order to create the height field the number of data points is needed
   // which uses the current size of the region, so the region sizes are saved
   // here
   heightFieldDescription.nbRows = regionSizeX;
   heightFieldDescription.nbColumns = regionSizeY;

   // For now, add a default thickness of 10 units to handle odd collision
   // cases
   heightFieldDescription.thickness = -10.0f;

   // Determine the number of datapoints inside of the posts array
   numPosts = regionSizeX * regionSizeY;

   // Initialize a height field sample array to store the posts data translated
   // for PhysX
   heightFieldSampleArray = new PxHeightFieldSample[numPosts];

   // Loop through all of the posts
   for (int i = 0; i < numPosts; i++)
   {
      // Copy the current height field posts into the sample array, while
      // translating them for PhysX
      // NOTE: The posts are scaled to match PhysX data, but as this can cause
      // a loss of precision, an expanding scale is used to preserve as much
      // precision as possible
      // NOTE: Both the incoming height field and the sample array have
      // row-major ordering, so the elements can be copied directly
      heightFieldSampleArray[i].height = (PxI16)(posts[i] /
         height_field_scale);

      // For now, differing materials are not supported in the height field, so
      // the default material indices are used
      heightFieldSampleArray[i].materialIndex0 = 1;
      heightFieldSampleArray[i].materialIndex1 = 1;
   }

   // Store the sample array inside of strided data for storage inside of the
   // height field description
   stridedData.data = heightFieldSampleArray;
   stridedData.stride = sizeof(PxHeightFieldSample);

   // Tell PhysX that a 16 bit integer height value will be used for the data
   heightFieldDescription.format = PxHeightFieldFormat::eS16_TM;

   // Store the sample array inside of the height field description
   heightFieldDescription.samples = stridedData;

   // TODO: Add in other ways of making the height field
   // Cooks the height field using the description that was just created
   heightField = px_cooking->createHeightField(heightFieldDescription,
      px_physics->getPhysicsInsertionCallback());

   // Check that the height field was successfully created
   if (heightField == NULL)
   {
      // Send error message to the user
      cerr << "Failed to create height field!\n";

      // Clean up resources and break out of the method, since the height field
      // doesn't exist
      delete heightFieldSampleArray;
      return;
   }

   // Use the height field, scale, and spacing of posts to create a height
   // field geometry
   heightFieldGeometry = new PxHeightFieldGeometry(heightField,
      PxMeshGeometryFlags(), height_field_scale, rowSpacing, columnSpacing);

   // Create a default material
   // TODO: Change this to be passed in by the function
   physxMaterial = px_physics->createMaterial(0.2f, 0.2f, 0.0f);

   // Use the geometry and material to create the terrain shape
   newShape = px_physics->createShape(*heightFieldGeometry, *physxMaterial);

   // Check if the scene already has a loaded terrain so that it can be removed
   // before the next terrain is loaded
   if (actor_map->containsKey(terrain_id))
   {
      // Remove the actor from the map of actors, but keep a reference so the
      // memory can be cleaned up after the actor has been removed from the
      // PhysX scene
      actor = (PhysXRigidActor*) actor_map->removeEntry(terrain_id);

      // Remove the actor from the PhysX scene
      px_scene->removeActor(*(actor->getActor()), false);

      // Clean up the memory used by the Terrain actor
      delete actor;
   }

   // Create a static actor to hold the terrain height map shape
   // TODO: Update ID
   actor = createActor(terrain_id->getValue(), "terrain", 0.0f, 0.0f, 0.0f,
      false);

   // Rotate the height map to the correct world position, this is needed
   // because the height map is just a list of heights and doesn't include the
   // position or orientation
   // TODO: Fix this to allow for mega regions
   newShape->setLocalPose(PxTransform(PxVec3(0.0f, 0.0f, 0.0f),
      PxQuat(0.5f, 0.5f, 0.5f, 0.5f)));

   // Add the shape for the terrain to the actor that has been added to the
   // scene
   actor->setShape(newShape);

   // Add the newly created actor to the scene
   px_scene->addActor(*(actor->getActor()));
}


PHYSX_API void addJoint(
   unsigned int jointID, unsigned int actorID1, unsigned int actorID2,
   float * actor1Pos, float * actor1Quat, float * actor2Pos,
   float * actor2Quat, float * linearLowerLimit, float * linearUpperLimit,
   float * angularLowerLimit, float * angularUpperLimit)
{
   PhysXJoint *                physXJoint;
   PhysXRigidActor *           actor1;
   PhysXRigidActor *           actor2;
   PxRigidActor *              rigidActor1;
   PxRigidActor *              rigidActor2;
   PxD6Joint *                 joint;
   PxJointLinearLimit *        jointLinearLimit;
   PxJointAngularLimitPair *   twistLimit;
   PxJointLimitCone *          swingLimits;
   PxTransform                 actor1Frame;
   PxTransform                 actor2Frame;
   float                       ySwingLimit;
   float                       zSwingLimit;

   // Check whether or not a joint with the given ID already exists;
   // can't have the same IDs for different joints
   physXJoint = (PhysXJoint *)joint_map->getValue(new atInt(jointID));
   if (physXJoint != NULL)
      return;

   // Get the actors associated with a joint from the given actor IDs
   actor1 = getActor(actorID1);
   actor2 = getActor(actorID2);

   // Check whether or not the given actor exists and get reference to
   // the PhysX rigid actor; otherwise rigid actor is NULL which would
   // indicate that the joint is attached to a point in the world frame
   if (actor1 != NULL)
      rigidActor1 = actor1->getRigidActor();
   else
      rigidActor1 = NULL;

   // Same here: check if given actor exists and get reference to rigid actor
   // or set to NULL to indicate this is a point in the world frame
   if (actor2 != NULL)
      rigidActor2 = actor2->getRigidActor();
   else
      rigidActor2 = NULL;

   // Create the transform for each actor's position and orientation
   // of the joint they are attached to
   actor1Frame = PxTransform(PxVec3(actor1Pos[0], actor1Pos[1], actor1Pos[2]),
      PxQuat(actor1Quat[0], actor1Quat[1], actor1Quat[2], actor1Quat[3]));
   actor2Frame = PxTransform(PxVec3(actor2Pos[0], actor2Pos[1], actor2Pos[2]),
      PxQuat(actor2Quat[0], actor2Quat[1], actor2Quat[2], actor2Quat[3]));

   // Create a new D6 joint between the given actors
   joint = PxD6JointCreate(
      *px_physics, rigidActor1, actor1Frame, rigidActor2, actor2Frame);

   // Indicate that this joint should be enforced, even under extreme duress
   joint->setProjectionLinearTolerance(0.1f);
   joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, true);

   // Adjust linear constraints of the joint based on the given limits
   // for each of the translational axes
   for (int i = 0; i < 3; i++)
   {
      // Check to see how the lower limit compares to the upper limit
      if (linearLowerLimit[i] == linearUpperLimit[i])
      {
         // The lower limit is the same as the upper limit, which means
         // that the axis should be locked
         joint->setMotion((PxD6Axis::Enum) i, PxD6Motion::eLOCKED);
      }
      else if (linearLowerLimit[i] > linearUpperLimit[i])
      {
         // The lower limit is greater than the upper limit, which means
         // that the axis should be free
         joint->setMotion((PxD6Axis::Enum) i, PxD6Motion::eFREE);
      }
      else
      {
         // The lower limit is less than the upper limit, which means
         // the axis should be limited, but not locked
         joint->setMotion((PxD6Axis::Enum) i, PxD6Motion::eLIMITED);

         // Limit the linear freedom by the difference in the limits
         // NOTE: In PhysX this causes all linear degrees of freedom to have
         // the same limit
         jointLinearLimit = new PxJointLinearLimit(
            px_physics->getTolerancesScale(),
            linearUpperLimit[i] - linearLowerLimit[i]);
         joint->setLinearLimit(*jointLinearLimit);
      }
   }

   // Adjust angular constraints of the joint based on the given limits
   // for each of the angular axes
   ySwingLimit = 0.0f;
   zSwingLimit = 0.0f;
   for (int i = 0; i < 3; i++)
   {
      // Check to see how the lower limit compares to the upper limit
      if (angularLowerLimit[i] == angularUpperLimit[i])
      {
         // The lower limit is the same as the upper limit, which means
         // that the axis should be locked
         joint->setMotion((PxD6Axis::Enum) (i + 3), PxD6Motion::eLOCKED);
      }
      else if (angularLowerLimit[i] > angularUpperLimit[i])
      {
         // The lower limit is greater than the upper limit, which means that
         // the axis should be free
         joint->setMotion((PxD6Axis::Enum) (i + 3), PxD6Motion::eFREE);
      }
      else
      {
         // The lower limit is less than the upper limit, which means that
         // the axis should be limited, but not locked
         joint->setMotion((PxD6Axis::Enum) (i + 3), PxD6Motion::eLIMITED);

         // Check to see which axis is limited
         if (i == 0)
         {
            // If the axis is being limited is the x-axis, use the twist limits
            twistLimit = new PxJointAngularLimitPair(
               angularLowerLimit[i], angularUpperLimit[i]);
            joint->setTwistLimit(*twistLimit);
         }
         else if (i == 1)
         {
            // This is a y-axis limit, which has to be set up at the same
            // time as the z-axis limit in a limit cone; so just store
            // the limit for now
            ySwingLimit = angularUpperLimit[i] - angularLowerLimit[i];
         }
         else
         {
            // This is a z-axis limit, which has to be set up at the same
            // time as the y-axis limit in a limit cone; so just store
            // the limit for now
            zSwingLimit = angularUpperLimit[i] - angularLowerLimit[i];
         }
      }
   }

   // Check to see if either y- and/or z-axis is supposed to be limited
   if (ySwingLimit > 0.0 || zSwingLimit > 0.0)
   {
      // Create a swing limit to represent limits around both axes
      swingLimits = new PxJointLimitCone(ySwingLimit, zSwingLimit);
      joint->setSwingLimit(*swingLimits);
   }

   // Create a new container to hold the joint information
   physXJoint = new PhysXJoint(joint, jointID, actorID1, actorID2);

   // Save reference to the new joint
   joint_map->addEntry(new atInt(jointID), physXJoint);
}


PHYSX_API void removeJoint(unsigned int id)
{
   PhysXJoint *   joint;

   // Remove the given joint from the map
   joint = (PhysXJoint *)joint_map->removeEntry(new atInt(id));

   // Clean up the joint if it existed
   if (joint != NULL)
      delete joint;
}


PHYSX_API void simulate(float time,
   unsigned int * updatedEntityCount, unsigned int * updatedCollisionCount)
{
   const PxActiveTransform *   activeTransforms;
   PxRigidDynamic *            actor;
   EntityProperties *          updatedActors;
   atInt *                     actorID;
   unsigned int                numTransforms;

   px_scene->lockRead();

   // Advance the world forward in time
   px_scene->simulate(time);

   // Allow the simulation to finish and indicate that it should
   // wait until it is completed
   px_scene->fetchResults(true);

   // Retrieve the array of actors that have been active since
   // the last simulation step
   numTransforms = 0;
   activeTransforms = px_scene->getActiveTransforms(numTransforms);

   // New array to keep track of the physical properties of the active
   // transforms
   updatedActors = new EntityProperties[max_updates];

   // Go through all active actors
   for (unsigned int i = 0; i < numTransforms; i++)
   {
      // We are only able to make a certain amount of updates for each step
      if (i >= max_updates)
         break;

      // Get the affected actor and its ID from its user data
      actor = (PxRigidDynamic *)activeTransforms[i].actor;
      actorID = reinterpret_cast<atInt *>(actor->userData);

      // TODO: temp use of prim types instead of Object type

      // Update the actor's position
      PxVec3 position = activeTransforms[i].actor2World.p;
      updatedActors[i].PositionX = position.x;
      updatedActors[i].PositionY = position.y;
      updatedActors[i].PositionZ = position.z;

      // Update the actor's orientation
      PxQuat rotation = activeTransforms[i].actor2World.q;
      updatedActors[i].RotationX = rotation.x;
      updatedActors[i].RotationY = rotation.y;
      updatedActors[i].RotationZ = rotation.z;
      updatedActors[i].RotationW = rotation.w;

      // Update the actor's velocity
      PxVec3 velocity = actor->getLinearVelocity();
      updatedActors[i].VelocityX = velocity.x;
      updatedActors[i].VelocityY = velocity.y;
      updatedActors[i].VelocityZ = velocity.z;

      // Update the actor's angular velocity
      PxVec3 angularVelocity = actor->getAngularVelocity();
      updatedActors[i].AngularVelocityX = angularVelocity.x;
      updatedActors[i].AngularVelocityY = angularVelocity.y;
      updatedActors[i].AngularVelocityZ = angularVelocity.z;

      // Save the actor's ID if one was saved in the actor's user data;
      // if the ID wasn't found, that means that this is an actor that
      // we are not keeping track of so just give it a default ID value
      if (actorID != NULL)
         updatedActors[i].ID = actorID->getValue();
      else
         updatedActors[i].ID = 0;

      // Save the physical properties of this actor in the update array;
      // this updates the array data in the calling application
      update_array[i] = updatedActors[i];
   }

   // Update the number of active transforms and collisions,
   // in this step, by reference
   *updatedEntityCount = numTransforms;
   px_collisions->getCollisions(updatedCollisionCount);
   px_scene->unlockRead();
}

