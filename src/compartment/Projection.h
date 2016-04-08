/*
 * Projection.h
 *
 *  Created on: Apr 5, 2016
 *      Author: shoops
 */

#ifndef COMPARTMENT_PROJECTION_H_
#define COMPARTMENT_PROJECTION_H_

#include "repast_hpc/SharedDiscreteSpace.h"

namespace ENISI
{
class FunctorInterface
{
public:
  typedef void (*Type)(std::set< repast::AgentId > & agentsToTest,
                       std::map< int, std::set<repast::AgentId > > & agentsToPush);

  virtual ~FunctorInterface() {};

  virtual void operator()(std::set< repast::AgentId > & agentsToTest,
                          std::map< int, std::set<repast::AgentId > > & agentsToPush) = 0;
};

template <class Callee>
class Functor : public FunctorInterface
{
private:
  /**
   * The pointer to the instance of the caller
   */
  Callee * mpInstance;   // pointer to object
  void (Callee::*mMethod)(std::set< repast::AgentId > & agentsToTest,
                          std::map< int, std::set<repast::AgentId > > & agentsToPush);
private:
  Functor():
    FunctorInterface(),
    mpInstance(NULL),
    mMethod(NULL)
  {}

public:
  Functor(Callee * pInstance,
          void (Callee::*method)(std::set< repast::AgentId > & agentsToTest,
                                 std::map< int, std::set<repast::AgentId > > & agentsToPush)):
    FunctorInterface(),
    mpInstance(pInstance),
    mMethod(method)
  {}

  virtual ~Functor() {}

  // override operator "()"
  virtual void operator()(std::set< repast::AgentId > & agentsToTest,
                          std::map< int, std::set<repast::AgentId > > & agentsToPush)
  {
    // execute member function
    (*mpInstance.*mMethod)(agentsToTest, agentsToPush);
  }
};

template<typename AgentType, typename GPTransformer, typename Adder>
class Projection: public repast::SharedDiscreteSpace< AgentType, GPTransformer, Adder >
{
  public:

  private:
    FunctorInterface * mpFunctor;

  protected:

  public:
    /**
     * Creates a projection with specified name.
     *
     * @param name the name of the projection. This must be unique
     * across projections
     */
    Projection(std::string name,
               repast::GridDimensions gridDims,
               std::vector<int> processDims,
               int buffer,
               boost::mpi::communicator* world) :
      repast::SharedDiscreteSpace< AgentType, GPTransformer, Adder >(name, gridDims, processDims, buffer, world),
      mpFunctor(NULL)
    {}

    virtual ~Projection()
    {
      if (mpFunctor != NULL) delete mpFunctor;
    }


    /**
     * Given a set of agents, gets the agents that this projection implementation must 'push' to
     * other processes. Generally spaces must push agents that are in 'buffer zones' and graphs
     * must push local agents that are vertices to master edges where the other vertex is non-
     * local. The results are returned per-process in the agentsToPush map.
     */
    virtual void getAgentsToPush(std::set<repast::AgentId> & agentsToTest,
                                 std::map<int, std::set<repast::AgentId> > & agentsToPush)
    {
      if (mpFunctor != NULL)
        {
          (*mpFunctor)(agentsToTest, agentsToPush);
        }

      repast::SharedDiscreteSpace< AgentType, GPTransformer, Adder >::getAgentsToPush(agentsToTest, agentsToPush);
    }

    void setFunctor(FunctorInterface * pFunctor)
    {
      mpFunctor = pFunctor;
    }
};

} /* namespace ENISI */

#endif /* COMPARTMENT_PROJECTION_H_ */
