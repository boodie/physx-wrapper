#ifndef PHYSX_JOINT_H
#define PHYSX_JOINT_H

#include <PxPhysicsAPI.h>

#include <atInt.h++>
#include <atItem.h++>

using namespace physx;

/// A container object for holding the corresponding PhysX joint as well
/// as additional meta-data.

class PhysXJoint : public atItem
{
   public:
      /// Constructor,
      ///
      /// @param joint The underlying PhysX joint that will be held by
      /// object.
      /// @param jointID The unique identifier for the joint.
      /// @param actorID1 The unique identifier for the first actor to
      /// which the joint is attached to.
      /// @param actorID2 The unique identifier for the second actor to
      /// which the joint is attached to.
      ///
      PhysXJoint(PxJoint * joint, unsigned int jointID,
         unsigned int actorID1, unsigned int actorID2);

      /// Deconstructor.
      ///
      ~PhysXJoint();

      /// Updates the unique identifier of the joint.
      ///
      /// @param id The new unique identifier of the joint.
      ///
      void   setID(unsigned int id);

      /// Returns the unique identifier of the joint.
      ///
      /// @return The unique identifier of the joint.
      ///
      atInt   getID();

      /// Updates the unique identifier of the first actor to which
      /// the joint is attached to.
      ///
      /// @param id The new unique identifier of first actor.
      ///
      void   setFirstActorID(unsigned int id);

      /// Returns the unique identifier of the first actor to which
      /// the joint is attached to.
      ///
      /// @return The unique identifier of the first actor.
      ///
      atInt   getFirstActorID();

      /// Updates the unique identifier of the second actor to which
      /// the joint is attached to.
      ///
      /// @param id The new unique identifier of second actor.
      ///
      void   setSecondActorID(unsigned int id);

      /// Returns the unique identifier of the second actor to which
      /// the joint is attached to.
      ///
      /// @return The unique identifier of the second actor.
      ///
      atInt   getSecondActorID();

      /// Updates the underlying PhysX joint held by this object.
      ///
      /// @param joint The new underlying PhysX joint.
      ///
      void   setJoint(PxJoint * joint);

      /// Returns the underlying PhysX joint held by this object.
      ///
      /// @return The underlying PhysX joint held by this object.
      ///
      PxJoint *   getJoint();


   protected:
      /// The unique identifier of the joint.
      atInt *   joint_id;

      /// The unique identifier of the first actor to which the joint
      /// is attached to.
      atInt *   actor1_id;

      /// The unique identifier of the second actor to which the joint
      /// is attached to.
      atInt *   actor2_id;

      /// The underlying PhysX joint being held by this object.
      PxJoint *   px_joint;
};

#endif
