#include "compartment/Compartment.h"

#include "ICompartmentLayer.h"

#include "grid/Properties.h"
#include "agent/Cytokine.h"
#include "agent/SharedValueLayer.h"
#include "Projection.h"

using namespace ENISI;

// static
const char* Compartment::Names[] = {"lumen", "epithilium", "lamina_propria", "gastric_lymph_node", NULL};

// static
Compartment* Compartment::INSTANCES[] = {NULL, NULL, NULL, NULL};

// static
Compartment* Compartment::instance(const Compartment::Type & type)
{
  Compartment **pInstance = &INSTANCES[type];

  if (*pInstance == NULL)
    {
      *pInstance = new Compartment(type);
    }

  return *pInstance;
}

Compartment::Compartment(const Type & type):
  mType(type),
  mProperties(),
  mDimensions(),
  mpLayer(NULL),
  mpSpaceBorders(NULL),
  mpGridBorders(NULL),
  mAdjacentCompartments(2, std::vector< Type >(2, INVALID)),
  mUniform(repast::Random::instance()->createUniDoubleGenerator(0.0, 1.0)),
  mCytokineMap(),
  mpDiffuserValues(NULL)
{
  std::string Name = Names[mType];

  Properties::getValue(Name + ".space.x", mProperties.spaceX);
  Properties::getValue(Name + ".space.y", mProperties.spaceY);
  Properties::getValue("grid.size", mProperties.gridSize);

  mProperties.gridX = ceil(mProperties.spaceX / mProperties.gridSize);
  mProperties.spaceX = mProperties.gridX * mProperties.gridSize;
  mProperties.gridY = ceil(mProperties.spaceY / mProperties.gridSize);
  mProperties.spaceY = mProperties.gridY * mProperties.gridSize;

  std::string borderLow = Properties::getValue(Name + ".border.y.low");
  mProperties.borderLowCompartment = Properties::toEnum(borderLow, Names, INVALID);
  mProperties.borderLowType = Properties::toEnum(borderLow, Borders::TypeNames, Borders::PERMIABLE);

  std::string borderHigh = Properties::getValue(Name + ".border.y.high");
  mProperties.borderHighCompartment = Properties::toEnum(borderHigh, Names, INVALID);
  mProperties.borderHighType = Properties::toEnum(borderHigh, Borders::TypeNames, Borders::PERMIABLE);

  std::vector< double > Origin(2, 0);
  std::vector< double > SpaceExtents(2);
  SpaceExtents[Borders::X] = mProperties.spaceX;
  SpaceExtents[Borders::Y] = mProperties.spaceY;
  std::vector< double > GridExtents(2);
  GridExtents[Borders::X] = mProperties.gridX;
  GridExtents[Borders::Y] = mProperties.gridY;

  mDimensions = repast::GridDimensions(Origin, SpaceExtents);
  repast::GridDimensions GridDimensions(Origin, GridExtents);

  mpLayer = new SharedLayer(Name, mDimensions, GridDimensions);

  mpSpaceBorders = new Borders(mDimensions);
  mpSpaceBorders->setBorderType(Borders::Y, Borders::LOW, mProperties.borderLowType);
  mpSpaceBorders->setBorderType(Borders::Y, Borders::HIGH, mProperties.borderHighType);

  mpGridBorders = new Borders(GridDimensions);
  mpGridBorders->setBorderType(Borders::Y, Borders::LOW, mProperties.borderLowType);
  mpGridBorders->setBorderType(Borders::Y, Borders::HIGH, mProperties.borderHighType);

  mAdjacentCompartments[Borders::X][Borders::LOW] = INVALID;
  mAdjacentCompartments[Borders::X][Borders::HIGH] = INVALID;
  mAdjacentCompartments[Borders::Y][Borders::LOW] = mProperties.borderLowCompartment;
  mAdjacentCompartments[Borders::Y][Borders::HIGH] = mProperties.borderHighCompartment;

  mpLayer->addCompartment(this);
}

Compartment::~Compartment()
{
  if (INSTANCES[mType] == this)
    {
      INSTANCES[mType] = NULL;
    }

  if (mpLayer != NULL) delete mpLayer;
  if (mpSpaceBorders != NULL) delete mpSpaceBorders;
  if (mpGridBorders != NULL) delete mpGridBorders;
}

const repast::GridDimensions & Compartment::dimensions() const
{
  return mDimensions;
}

const repast::GridDimensions & Compartment::localSpaceDimensions() const
{
  return mpLayer->spaceDimensions();
}

const repast::GridDimensions & Compartment::localGridDimensions() const
{
  return mpLayer->gridDimensions();
}

const Borders * Compartment::spaceBorders() const
{
  return mpSpaceBorders;
}

const Borders * Compartment::gridBorders() const
{
  return mpGridBorders;
}

const Compartment * Compartment::getAdjacentCompartment(const Borders::Coodinate &coordinate, const Borders::Side & side) const
{
  return instance(mAdjacentCompartments[coordinate][side]);
}

Iterator Compartment::begin()
{
  return Iterator(mpLayer->gridDimensions());
}

std::vector< double > Compartment::gridToSpace(const std::vector< int > & grid) const
{
  return mpLayer->gridToSpace(grid);
}

std::vector< int > Compartment::spaceToGrid(const std::vector< double > & space) const
{
  return mpLayer->spaceToGrid(space);
}

void Compartment::getLocation(const repast::AgentId & id, std::vector<double> & Location) const
{
  mpLayer->getLocation(id, Location);
}

bool Compartment::moveTo(const repast::AgentId &id, repast::Point< double > &pt)
{
  return moveTo(id, pt.coords());
}

bool Compartment::moveTo(const repast::AgentId &id, std::vector< double > &pt)
{
  // We need to transform the point possibly to the coordinates of an adjacent compartment.
  Compartment * pTarget = transform(pt);

  if (pTarget == this)
    {
      return mpLayer->moveTo(id, pt);
    }

  bool success = false;

  if (pTarget != NULL)
    {
      Agent * pAgent = mpLayer->getAgent(id);
      success = pTarget->addAgent(pAgent, pt);
      removeAgent(pAgent);
    }

  return success;
}

Compartment * Compartment::transform(std::vector< double > & pt) const
{
  mpSpaceBorders->transform(pt);

  std::vector< Borders::BoundState > BoundState(2);

  if (mpSpaceBorders->boundsCheck(pt, &BoundState))
    {
      return const_cast< Compartment * >(this);
    }

  // We are at the compartment boundaries;
  size_t i = Borders::X;

  std::vector<Borders::BoundState>::const_iterator itState = BoundState.begin();
  std::vector<Borders::BoundState>::const_iterator endState = BoundState.end();
  std::vector<double>::iterator itIn = pt.begin();

  Compartment * pTarget = NULL;

  for (; itState != endState && pTarget == NULL; ++itState, ++itIn, ++i)
    {
      switch (*itState)
        {
          case Borders::OUT_LOW:
            if (mAdjacentCompartments[i][Borders::LOW] != INVALID)
              {
                pTarget = instance(mAdjacentCompartments[i][Borders::LOW]);
                const repast::GridDimensions & Dimensions = pTarget->dimensions();
                *itIn += Dimensions.origin(i) + Dimensions.extents(i) - mDimensions.origin(i);
              }

            break;

          case Borders::OUT_HIGH:
            if (mAdjacentCompartments[i][Borders::HIGH] != INVALID)
              {
                pTarget = instance(mAdjacentCompartments[i][Borders::HIGH]);
                const repast::GridDimensions & Dimensions = pTarget->dimensions();
                *itIn += Dimensions.origin(i) - mDimensions.extents(i);
              }

            break;

          case Borders::INBOUND:
            break;
        }
    }

  return pTarget;
}

Compartment * Compartment::transform(std::vector< int > & pt) const
{
  mpGridBorders->transform(pt);

  std::vector< Borders::BoundState > BoundState(2);

  if (mpGridBorders->boundsCheck(pt, &BoundState))
    {
      return const_cast< Compartment * >(this);
    }

  // We are at the compartment boundaries;
  size_t i = Borders::X;

  std::vector<Borders::BoundState>::const_iterator itState = BoundState.begin();
  std::vector<Borders::BoundState>::const_iterator endState = BoundState.end();
  std::vector< int >::iterator itIn = pt.begin();

  Compartment * pTarget = NULL;

  for (; itState != endState; ++itState, ++itIn, ++i)
    {
      if (pTarget != NULL)
        {
          continue;
        }

      switch (*itState)
        {
          case Borders::OUT_LOW:
            if (mAdjacentCompartments[i][Borders::LOW] != INVALID)
              {
                pTarget = instance(mAdjacentCompartments[i][Borders::LOW]);
                const repast::GridDimensions & Dimensions = pTarget->dimensions();
                *itIn += Dimensions.origin(i) + Dimensions.extents(i) - mDimensions.origin(i);
              }

            break;

          case Borders::OUT_HIGH:
            if (mAdjacentCompartments[i][Borders::HIGH] != INVALID)
              {
                pTarget = instance(mAdjacentCompartments[i][Borders::HIGH]);
                const repast::GridDimensions & Dimensions = pTarget->dimensions();
                *itIn += Dimensions.origin(i) - mDimensions.extents(i);
              }

            break;

          case Borders::INBOUND:
            break;
        }
    }

  return pTarget;
}

bool Compartment::moveRandom(const repast::AgentId &id, const double & maxSpeed)
{
  static double fullCircle = 2 * M_PI; // in radians
  double angle = fullCircle * mUniform.next();
  double radius = maxSpeed * mUniform.next();

  std::vector< double > Location(2);

  mpLayer->getLocation(id, Location);
  Location[0] += radius * cos(angle);
  Location[1] += radius * sin(angle);

  mpSpaceBorders->transform(Location);

  std::vector< Borders::BoundState > BoundState(2);

  if (!mpSpaceBorders->boundsCheck(Location, &BoundState))
    {
      // We are at the compartment boundaries;
      // Since this is a random move we reflect at the compartment border

      size_t i = Borders::X;

      std::vector<Borders::BoundState>::const_iterator itState = BoundState.begin();
      std::vector<Borders::BoundState>::const_iterator endState = BoundState.end();
      std::vector<double>::iterator itLocation = Location.begin();

      for (; itState != endState; ++itState, ++itLocation, ++i)
        {
          switch (*itState)
            {
              case Borders::OUT_LOW:
                *itLocation = mDimensions.origin(i) - mpSpaceBorders->distanceFromBorder(Location, (Borders::Coodinate) i, Borders::LOW);
                break;

              case Borders::OUT_HIGH:
                *itLocation = mDimensions.origin(i) + mDimensions.extents(i) - mpSpaceBorders->distanceFromBorder(Location, (Borders::Coodinate) i, Borders::HIGH);
                break;

              case Borders::INBOUND:
                break;
            }
        }
    }

  return mpLayer->moveTo(id, Location);
}

bool Compartment::addAgent(Agent * agent, const std::vector< double > & pt)
{
  return mpLayer->addAgent(agent, pt);
}

bool Compartment::addAgentToRandomLocation(Agent * agent)
{
  return mpLayer->addAgentToRandomLocation(agent);
}

void Compartment::removeAgent(Agent * pAgent)
{
  mpLayer->removeAgent(pAgent);
}

void Compartment::getNeighbors(const repast::Point< int > &pt,
                               unsigned int range,
                               std::vector< Agent * > &out)
{
  mpLayer->getNeighbors(pt, range, out);
}

void Compartment::getNeighbors(const repast::Point< int > &pt,
                               unsigned int range,
                               const int & types,
                               std::vector< Agent * > &out)
{
  mpLayer->getNeighbors(pt, range, types, out);
}

void Compartment::getAgents(const repast::Point< int > &pt,
                            std::vector< Agent * > &out)
{
  mpLayer->getAgents(pt, out);
}

void Compartment::getAgents(const repast::Point< int > &pt,
                            const int & types,
                            std::vector< Agent * > &out)
{
  mpLayer->getAgents(pt, types, out);
}

void Compartment::getAgents(const repast::Point< int > &pt, const int & xOffset, const int & yOffset, std::vector< Agent * > &out)
{
  std::vector< int > Location = pt.coords();
  Location[Borders::X] += xOffset;
  Location[Borders::Y] += yOffset;

  Compartment * pTarget = transform(Location);

  if (pTarget == this)
    {
      mpLayer->getAgents(pt, out);
    }
  else if (pTarget != NULL)
    {
      pTarget->getAgents(pt, out);
    }

  return ;
}

void Compartment::getAgents(const repast::Point< int > &pt, const int & xOffset, const int & yOffset, const int & types, std::vector< Agent * > &out)
{
  std::vector< int > Location = pt.coords();
  Location[Borders::X] += xOffset;
  Location[Borders::Y] += yOffset;

  Compartment * pTarget = transform(Location);

  if (pTarget == this)
    {
      mpLayer->getAgents(pt, types, out);
    }
  else if (pTarget != NULL)
    {
      pTarget->getAgents(pt, types, out);
    }

  return ;

}

size_t Compartment::addCytokine(const std::string & name)
{
  Cytokine * pCytokine = new Cytokine(getName() + "." + name);

  pCytokine->setIndex(mCytokines.size());
  mCytokines.push_back(pCytokine);
  mCytokineMap.insert(std::make_pair(name, pCytokine->getIndex()));

  return pCytokine->getIndex();
}

const Cytokine * Compartment::getCytokine(const std::string & name) const
{
  std::map< std::string, size_t >::const_iterator found = mCytokineMap.find(name);

  if (found != mCytokineMap.end())
    {
      return mCytokines[found->second];
    }

  return NULL;
}

const std::vector< Cytokine * > & Compartment::getCytokines() const
{
  return mCytokines;
}

std::vector< double > & Compartment::cytokineValues(const repast::Point< int > & pt)
{
  if (this->localGridDimensions().contains(pt))
    {
      return mpDiffuserValues->operator[](pt);
    }

  // We loop through all non local agents and check whether they contain the value
  SharedLayer::Context::const_state_aware_iterator it = mpLayer->getValueContext().begin(SharedLayer::Context::NON_LOCAL);
  SharedLayer::Context::const_state_aware_iterator end = mpLayer->getValueContext().end(SharedLayer::Context::NON_LOCAL);

  std::vector< double > * pFound = NULL;

  for (; it != end && pFound == NULL; ++it)
    {
      pFound = static_cast< SharedValueLayer * >(&**it)->tryLocation(pt);
    }

  if (pFound != NULL)
    {
      return *pFound;
    }

  throw std::runtime_error("cytokine value not found");

  static std::vector< double > NaN(mCytokines.size(), std::numeric_limits< double >::quiet_NaN());
  return NaN;
}

double & Compartment::cytokineValue(const std::string & name, const repast::Point< int > & pt)
{
  std::vector< int > Location = pt.coords();

  Compartment * pTarget = transform(Location);

  if (pTarget == this)
    {
      return cytokineValues(Location)[mCytokineMap[name]];
    }
  else if (pTarget != NULL)
    {
      return pTarget->cytokineValue(name, Location);
    }

  throw std::runtime_error("cytokine value not found");

  static double NaN = std::numeric_limits< double >::quiet_NaN();
  return NaN;
}

double & Compartment::cytokineValue(const std::string & name, const repast::Point< int > & pt, const int & xOffset, const int & yOffset)
{
  std::vector< int > Location = pt.coords();
  Location[Borders::X] += xOffset;
  Location[Borders::Y] += yOffset;

  return cytokineValue(name, Location);
}

void Compartment::initializeDiffuserData()
{
  if (mCytokineMap.empty()) return;

  if (mpDiffuserValues == NULL)
    {
      mpDiffuserValues = new SharedValueLayer(Agent::DiffuserValues, mType, mCytokineMap.size());
      mpLayer->addDiffuserValues(mpDiffuserValues);

      std::vector< double > InitialValues(mCytokineMap.size());
      std::vector< double >::iterator itInitialValue = InitialValues.begin();
      std::vector< Cytokine * >::const_iterator it = mCytokines.begin();
      std::vector< Cytokine * >::const_iterator end = mCytokines.end();

      for (; it != end; ++it, ++itInitialValue)
        {
          *itInitialValue = (*it)->getInitialValue();
        }

      for (Iterator itGrid = begin(); itGrid; itGrid.next())
        {
          mpDiffuserValues->operator[](*itGrid) = InitialValues;
        }
    }

  synchronizeDiffuser();
}

SharedValueLayer * Compartment::getDiffuserData()
{
  return mpDiffuserValues;
}

/*
std::vector< double > & Compartment::operator[](const repast::Point< int > & location)
{
  return mpDiffuserValues->operator[](location);
}

const std::vector< double > & Compartment::operator[](const repast::Point< int > & location) const
{
  return mpDiffuserValues->operator[](location);
}

const std::vector< double > & Compartment::operator[](const repast::Point< double > & location) const
{
  return operator[](mpLayer->spaceToGrid(location.coords()));
}
*/

void Compartment::synchronizeCells()
{
  mpLayer->synchronizeCells();
}

void Compartment::getBorderCellsToPush(std::set<repast::AgentId> & /* agentsToTest */,
                                       std::map< int, std::set< repast::AgentId > > & agentsToPush)
{
  std::vector< Borders::Coodinate > Coordinates;
  std::vector< Borders::Side > Sides;

  repast::Point< double > Out = localGridDimensions().origin();

  if (mAdjacentCompartments[Borders::X][Borders::LOW] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::X, Borders::LOW)) < 0.5)
    {
      Coordinates.push_back(Borders::X);
      Sides.push_back(Borders::LOW);
    }

  if (mAdjacentCompartments[Borders::Y][Borders::LOW] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::Y, Borders::LOW)) < 0.5)
    {
      Coordinates.push_back(Borders::Y);
      Sides.push_back(Borders::LOW);
    }

  Out.add(localGridDimensions().extents());

  if (mAdjacentCompartments[Borders::X][Borders::HIGH] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::X, Borders::HIGH)) < 0.5)
    {
      Coordinates.push_back(Borders::X);
      Sides.push_back(Borders::HIGH);
    }

  if (mAdjacentCompartments[Borders::Y][Borders::HIGH] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::Y, Borders::HIGH)) < 0.5)
    {
      Coordinates.push_back(Borders::Y);
      Sides.push_back(Borders::HIGH);
    }

  std::vector< Borders::Coodinate >::const_iterator it = Coordinates.begin();
  std::vector< Borders::Coodinate >::const_iterator end = Coordinates.end();
  std::vector< Borders::Side >::const_iterator itSide = Sides.begin();

  for (; it != end; ++it, ++itSide)
    {
      // Add all local agent Ids which are near the border to be pushed;
      getBorderCellsToPush(*it, *itSide, agentsToPush);
    }
}

void Compartment::getBorderCellsToPush(const Borders::Coodinate & coordinate,
                                       const Borders::Side & side,
                                       std::map< int, std::set< repast::AgentId > > & agentsToPush)
{
  Borders::Coodinate OtherCordinate = (coordinate == Borders::Y) ? Borders::X : Borders::Y;

  int xOffset = (coordinate != Borders::X) ? 0 : (side == Borders::HIGH) ? +1 : -1;
  int yOffset = (coordinate != Borders::Y) ? 0 : (side == Borders::HIGH) ? +1 : -1;

  Iterator itPoint(localGridDimensions());
  if (side == Borders::HIGH)
    {
      for (int i = 0, imax = localGridDimensions().extents(coordinate); i < imax; ++i)
        {
          itPoint.next(OtherCordinate);
        }
    }

  for (; itPoint; itPoint.next(coordinate))
    {
      int TargetRank = mpLayer->getRank(itPoint->coords(), xOffset, yOffset);
      std::map< int, std::set< repast::AgentId > >::iterator found = agentsToPush.find(TargetRank);

      if (found == agentsToPush.end())
        {
          found = agentsToPush.insert(std::make_pair(TargetRank, std::set< repast::AgentId >())).first;
        }

      std::vector< Agent * > out;
      mpLayer->getAgents(*itPoint, out);

      std::vector< Agent * >::const_iterator it = out.begin();
      std::vector< Agent * >::const_iterator end = out.end();

      for (; it != end; ++it)
        {
          found->second.insert((*it)->getId());
        }
    }
}

void Compartment::getBorderValuesToPush(std::set<repast::AgentId> & /* agentsToTest */,
                                        std::map< int, std::set< repast::AgentId > > & agentsToPush)
{
  std::vector< Borders::Coodinate > Coordinates;
  std::vector< Borders::Side > Sides;

  repast::Point< double > Out = localGridDimensions().origin();

  if (mAdjacentCompartments[Borders::X][Borders::LOW] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::X, Borders::LOW)) < 0.5)
    {
      Coordinates.push_back(Borders::X);
      Sides.push_back(Borders::LOW);
    }

  if (mAdjacentCompartments[Borders::Y][Borders::LOW] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::Y, Borders::LOW)) < 0.5)
    {
      Coordinates.push_back(Borders::Y);
      Sides.push_back(Borders::LOW);
    }

  Out.add(localGridDimensions().extents());

  if (mAdjacentCompartments[Borders::X][Borders::HIGH] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::X, Borders::HIGH)) < 0.5)
    {
      Coordinates.push_back(Borders::X);
      Sides.push_back(Borders::HIGH);
    }

  if (mAdjacentCompartments[Borders::Y][Borders::HIGH] != INVALID &&
      fabs(mpGridBorders->distanceFromBorder(Out.coords(), Borders::Y, Borders::HIGH)) < 0.5)
    {
      Coordinates.push_back(Borders::Y);
      Sides.push_back(Borders::HIGH);
    }

  std::vector< Borders::Coodinate >::const_iterator it = Coordinates.begin();
  std::vector< Borders::Coodinate >::const_iterator end = Coordinates.end();
  std::vector< Borders::Side >::const_iterator itSide = Sides.begin();

  for (; it != end; ++it, ++itSide)
    {
      // Add all local agent Ids which are near the border to be pushed;
      getBorderValuesToPush(*it, *itSide, agentsToPush);
    }
}

void Compartment::getBorderValuesToPush(const Borders::Coodinate & coordinate,
                                        const Borders::Side & side,
                                        std::map< int, std::set< repast::AgentId > > & agentsToPush)
{
  Borders::Coodinate OtherCordinate = (coordinate == Borders::Y) ? Borders::X : Borders::Y;

  int xOffset = (coordinate != Borders::X) ? 0 : (side == Borders::HIGH) ? +1 : -1;
  int yOffset = (coordinate != Borders::Y) ? 0 : (side == Borders::HIGH) ? +1 : -1;

  Iterator itPoint(localGridDimensions());

  if (side == Borders::HIGH)
    {
      for (int i = 0, imax = localGridDimensions().extents(coordinate); i < imax; ++i)
        {
          itPoint.next(OtherCordinate);
        }
    }

  std::set< int > Targets;

  for (; itPoint; itPoint.next(coordinate))
    {
      Targets.insert(mpLayer->getRank(itPoint->coords(), xOffset, yOffset));
    }

  repast::AgentId Id = mpDiffuserValues->getId();

  std::set< int >::const_iterator itTarget = Targets.begin();
  std::set< int >::const_iterator endTarget = Targets.end();

  for (; itTarget != endTarget; ++itTarget)
    {
      std::map< int, std::set< repast::AgentId > >::iterator found = agentsToPush.find(*itTarget);

      if (found == agentsToPush.end())
        {
          found = agentsToPush.insert(std::make_pair(*itTarget, std::set< repast::AgentId >())).first;
        }

      found->second.insert(Id);
    }
}

void Compartment::synchronizeDiffuser()
{
  mpLayer->synchronizeDiffuser();

  // We loop through all non local agents an update the local diffuser values.
  SharedLayer::Context::const_state_aware_iterator it = mpLayer->getValueContext().begin(SharedLayer::Context::NON_LOCAL);
  SharedLayer::Context::const_state_aware_iterator end = mpLayer->getValueContext().end(SharedLayer::Context::NON_LOCAL);

  for (; it != end; ++it)
    {
      mpDiffuserValues->updateBufferValues(*static_cast< SharedValueLayer * >(&**it),
                                           *mpGridBorders);
    }
}

const Compartment::Type & Compartment::getType() const
{
  return mType;
}

size_t Compartment::localCount(const size_t & globalCount)
{
  size_t rank = repast::RepastProcess::instance()->rank();
  size_t worldSize = repast::RepastProcess::instance()->worldSize();

  size_t LocalCount = globalCount / worldSize;

  if (rank < globalCount - LocalCount * worldSize) LocalCount++;

  return LocalCount;
}

size_t Compartment::getRank(const std::vector< int > & location) const
{
  std::vector< int > Location = location;

  Compartment * pTarget = transform(Location);

  if (pTarget == this)
    {
      return mpLayer->getRank(Location, 0, 0);
    }
  else if (pTarget != NULL)
    {
      pTarget->getRank(Location);
    }

  return (size_t) INVALID;
}

size_t Compartment::getRank(const std::vector< int > & location, const int & xOffset, const int & yOffset) const
{
  std::vector< int > Location = location;
  Location[Borders::X] += xOffset;
  Location[Borders::Y] += yOffset;

  return getRank(Location);
}

std::string Compartment::getName() const
{
  return Names[mType];
}

