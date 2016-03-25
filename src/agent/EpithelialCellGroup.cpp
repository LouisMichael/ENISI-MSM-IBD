#include "EpithelialCellGroup.h"

#include "grid/Borders.h"
#include "compartment/Compartment.h"
#include "agent/Cytokines.h"

using namespace ENISI;

EpithelialCellGroup::EpithelialCellGroup(Compartment * pCompartment, const size_t & count):
  mpCompartment(pCompartment)
{
  for (size_t i = 0; i < count; i++)
    {
      mpCompartment->addAgentToRandomLocation(new Agent(Agent::EpithelialCell, EpithelialCellState::HEALTHY));
    }
}

void EpithelialCellGroup::act()
{
  for (Compartment::GridIterator it = mpCompartment->begin(); it; it.next())
    {
      act(*it);
    }
}


void EpithelialCellGroup::act(const repast::Point<int> & pt)
{
  std::vector< Agent * > EpithelialCells;
  mpCompartment->getAgents(pt, Agent::EpithelialCell, EpithelialCells);
  std::vector< Agent * >::iterator it = EpithelialCells.begin();
  std::vector< Agent * >::iterator end = EpithelialCells.end();

  std::vector< Agent * > Bacteria;
  // TODO CRITICAL Retrieve Bacteria cells in neighboring compartment if appropriate;

  StateCount BacteriaStateCount;
  CountStates(Agent::Bacteria, Bacteria, BacteriaStateCount);

  std::vector< Agent * > Tcells;
  // TODO CRITICAL Retrieve Tcells in neighboring compartment if appropriate;

  StateCount TcellsCellStateCount;
  CountStates(Agent::Tcell, Tcells, TcellsCellStateCount);

  for (; it != end; ++it)
    {
      Agent * pAgent = *it;
      EpithelialCellState::State state = (EpithelialCellState::State) pAgent->getState();

      if (state == EpithelialCellState::DEAD) continue;

      EpithelialCellState::State newState = state;

      unsigned int infectiousBacteriaCount = BacteriaStateCount[BacteriaState::INFECTIOUS];
      unsigned int tolegenicBacteriaCount = BacteriaStateCount[BacteriaState::TOLEROGENIC];

      //Rules 9 and 10
      unsigned int th17Count = TcellsCellStateCount[TcellState::TH17]; //Rule 10 when Th17 is in contact
      unsigned int th1Count = TcellsCellStateCount[TcellState::TH1]; //RUle 9 when Th1 is in contact

      if (infectiousBacteriaCount && state == EpithelialCellState::HEALTHY)
        {
          newState = EpithelialCellState::DAMAGED;
        }
      else if (tolegenicBacteriaCount && state == EpithelialCellState::HEALTHY)
        {
          newState = EpithelialCellState::HEALTHY;
        }
      else if (th17Count && state == EpithelialCellState::HEALTHY
               && mpCompartment->getType() == Compartment::lamina_propria) // TODO CRITICAL This will never be true
        {
          newState = EpithelialCellState::DAMAGED; /*Rule 10*/
          /* CHECK : Here there should be a function for information regarding the Layer,
          for eg. This rule requires the state transition when TH17 in LaminaPropria is in contact with E at 'Epithelium and LaminaPropria' membrane*/
        }
      else if (th1Count && state == EpithelialCellState::HEALTHY
               && mpCompartment->getType() == Compartment::lamina_propria) // TODO CRITICAL This will never be true
        {
          newState = EpithelialCellState::DAMAGED; /*Rule 9*/
          /* CHECK : Here there should be a function for information regarding the Layer,
          for eg. This rule requires the state transition when TH1 in LaminaPropria is in contact with E at 'Epithelium and LaminaPropria' membrane*/
        }
      else if (state == EpithelialCellState::HEALTHY &&
          mpCompartment->getType() == Compartment::epithilium) // TODO CRITICAL This will always be true
        {
          // addCellAt(state, newLoc);/* Rule 8*/
          mpCompartment->removeAgent(pAgent); /*Rule 11*/
        }

      if (newState == EpithelialCellState::DAMAGED)
        {
          Cytokines::CytoMap & cytoMap = Cytokines::instance().map();
          cytoMap["IL6"].first->setValueAtCoord(70, pt);
          cytoMap["IL12"].first->setValueAtCoord(70, pt);
        }

      pAgent->setState(newState);

      // TODO CRITICAL Determine the maximum speed
      double MaxSpeed = 1.0;
      mpCompartment->moveRandom(pAgent->getId(), MaxSpeed);
    }
}
