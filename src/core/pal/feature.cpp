/*
 *   libpal - Automated Placement of Labels Library
 *
 *   Copyright (C) 2008 Maxence Laurent, MIS-TIC, HEIG-VD
 *                      University of Applied Sciences, Western Switzerland
 *                      http://www.hes-so.ch
 *
 *   Contact:
 *      maxence.laurent <at> heig-vd <dot> ch
 *    or
 *      eric.taillard <at> heig-vd <dot> ch
 *
 * This file is part of libpal.
 *
 * libpal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libpal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libpal.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qgsgeometry.h"
#include "pal.h"
#include "layer.h"
#include "feature.h"
#include "geomfunction.h"
#include "labelposition.h"
#include "pointset.h"
#include "util.h"
#include "qgis.h"
#include "qgsgeos.h"
#include "qgsmessagelog.h"
#include "costcalculator.h"
#include "qgsgeometryutils.h"
#include <QLinkedList>
#include <cmath>
#include <cfloat>

using namespace pal;

FeaturePart::FeaturePart( QgsLabelFeature *feat, const GEOSGeometry *geom )
  : mLF( feat )
{
  // we'll remove const, but we won't modify that geometry
  mGeos = const_cast<GEOSGeometry *>( geom );
  mOwnsGeom = false; // geometry is owned by Feature class

  extractCoords( geom );

  holeOf = nullptr;
  for ( int i = 0; i < mHoles.count(); i++ )
  {
    mHoles.at( i )->holeOf = this;
  }

}

FeaturePart::FeaturePart( const FeaturePart &other )
  : PointSet( other )
  , mLF( other.mLF )
{
  for ( const FeaturePart *hole : qgis::as_const( other.mHoles ) )
  {
    mHoles << new FeaturePart( *hole );
    mHoles.last()->holeOf = this;
  }
}

FeaturePart::~FeaturePart()
{
  // X and Y are deleted in PointSet

  qDeleteAll( mHoles );
  mHoles.clear();
}

void FeaturePart::extractCoords( const GEOSGeometry *geom )
{
  const GEOSCoordSequence *coordSeq = nullptr;
  GEOSContextHandle_t geosctxt = QgsGeos::getGEOSHandler();

  type = GEOSGeomTypeId_r( geosctxt, geom );

  if ( type == GEOS_POLYGON )
  {
    if ( GEOSGetNumInteriorRings_r( geosctxt, geom ) > 0 )
    {
      int numHoles = GEOSGetNumInteriorRings_r( geosctxt, geom );

      for ( int i = 0; i < numHoles; ++i )
      {
        const GEOSGeometry *interior = GEOSGetInteriorRingN_r( geosctxt, geom, i );
        FeaturePart *hole = new FeaturePart( mLF, interior );
        hole->holeOf = nullptr;
        // possibly not needed. it's not done for the exterior ring, so I'm not sure
        // why it's just done here...
        GeomFunction::reorderPolygon( hole->nbPoints, hole->x, hole->y );

        mHoles << hole;
      }
    }

    // use exterior ring for the extraction of coordinates that follows
    geom = GEOSGetExteriorRing_r( geosctxt, geom );
  }
  else
  {
    qDeleteAll( mHoles );
    mHoles.clear();
  }

  // find out number of points
  nbPoints = GEOSGetNumCoordinates_r( geosctxt, geom );
  coordSeq = GEOSGeom_getCoordSeq_r( geosctxt, geom );

  // initialize bounding box
  xmin = ymin = std::numeric_limits<double>::max();
  xmax = ymax = std::numeric_limits<double>::lowest();

  // initialize coordinate arrays
  deleteCoords();
  x.resize( nbPoints );
  y.resize( nbPoints );

  for ( int i = 0; i < nbPoints; ++i )
  {
    GEOSCoordSeq_getX_r( geosctxt, coordSeq, i, &x[i] );
    GEOSCoordSeq_getY_r( geosctxt, coordSeq, i, &y[i] );

    xmax = x[i] > xmax ? x[i] : xmax;
    xmin = x[i] < xmin ? x[i] : xmin;

    ymax = y[i] > ymax ? y[i] : ymax;
    ymin = y[i] < ymin ? y[i] : ymin;
  }
}

Layer *FeaturePart::layer()
{
  return mLF->layer();
}

QgsFeatureId FeaturePart::featureId() const
{
  return mLF->id();
}

bool FeaturePart::hasSameLabelFeatureAs( FeaturePart *part ) const
{
  if ( !part )
    return false;

  if ( mLF->layer()->name() != part->layer()->name() )
    return false;

  if ( mLF->id() == part->featureId() )
    return true;

  // any part of joined features are also treated as having the same label feature
  int connectedFeatureId = mLF->layer()->connectedFeatureId( mLF->id() );
  return connectedFeatureId >= 0 && connectedFeatureId == mLF->layer()->connectedFeatureId( part->featureId() );
}

LabelPosition::Quadrant FeaturePart::quadrantFromOffset() const
{
  QPointF quadOffset = mLF->quadOffset();
  qreal quadOffsetX = quadOffset.x(), quadOffsetY = quadOffset.y();

  if ( quadOffsetX < 0 )
  {
    if ( quadOffsetY < 0 )
    {
      return LabelPosition::QuadrantAboveLeft;
    }
    else if ( quadOffsetY > 0 )
    {
      return LabelPosition::QuadrantBelowLeft;
    }
    else
    {
      return LabelPosition::QuadrantLeft;
    }
  }
  else  if ( quadOffsetX > 0 )
  {
    if ( quadOffsetY < 0 )
    {
      return LabelPosition::QuadrantAboveRight;
    }
    else if ( quadOffsetY > 0 )
    {
      return LabelPosition::QuadrantBelowRight;
    }
    else
    {
      return LabelPosition::QuadrantRight;
    }
  }
  else
  {
    if ( quadOffsetY < 0 )
    {
      return LabelPosition::QuadrantAbove;
    }
    else if ( quadOffsetY > 0 )
    {
      return LabelPosition::QuadrantBelow;
    }
    else
    {
      return LabelPosition::QuadrantOver;
    }
  }
}

int FeaturePart::totalRepeats() const
{
  return mTotalRepeats;
}

void FeaturePart::setTotalRepeats( int totalRepeats )
{
  mTotalRepeats = totalRepeats;
}

int FeaturePart::createCandidatesOverPoint( double x, double y, QList< LabelPosition *> &lPos, double angle )
{
  int nbp = 1;

  // get from feature
  double labelW = getLabelWidth();
  double labelH = getLabelHeight();

  double cost = 0.0001;
  int id = 0;

  double xdiff = -labelW / 2.0;
  double ydiff = -labelH / 2.0;

  if ( !qgsDoubleNear( mLF->quadOffset().x(), 0.0 ) )
  {
    xdiff += labelW / 2.0 * mLF->quadOffset().x();
  }
  if ( !qgsDoubleNear( mLF->quadOffset().y(), 0.0 ) )
  {
    ydiff += labelH / 2.0 * mLF->quadOffset().y();
  }

  if ( ! mLF->hasFixedPosition() )
  {
    if ( !qgsDoubleNear( angle, 0.0 ) )
    {
      double xd = xdiff * std::cos( angle ) - ydiff * std::sin( angle );
      double yd = xdiff * std::sin( angle ) + ydiff * std::cos( angle );
      xdiff = xd;
      ydiff = yd;
    }
  }

  if ( mLF->layer()->arrangement() == QgsPalLayerSettings::AroundPoint )
  {
    //if in "around point" placement mode, then we use the label distance to determine
    //the label's offset
    if ( qgsDoubleNear( mLF->quadOffset().x(), 0.0 ) )
    {
      ydiff += mLF->quadOffset().y() * mLF->distLabel();
    }
    else if ( qgsDoubleNear( mLF->quadOffset().y(), 0.0 ) )
    {
      xdiff += mLF->quadOffset().x() * mLF->distLabel();
    }
    else
    {
      xdiff += mLF->quadOffset().x() * M_SQRT1_2 * mLF->distLabel();
      ydiff += mLF->quadOffset().y() * M_SQRT1_2 * mLF->distLabel();
    }
  }
  else
  {
    if ( !qgsDoubleNear( mLF->positionOffset().x(), 0.0 ) )
    {
      xdiff += mLF->positionOffset().x();
    }
    if ( !qgsDoubleNear( mLF->positionOffset().y(), 0.0 ) )
    {
      ydiff += mLF->positionOffset().y();
    }
  }

  double lx = x + xdiff;
  double ly = y + ydiff;

  if ( mLF->permissibleZonePrepared() )
  {
    if ( !GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), lx, ly, labelW, labelH, angle ) )
    {
      return 0;
    }
  }

  lPos << new LabelPosition( id, lx, ly, labelW, labelH, angle, cost, this, false, quadrantFromOffset() );
  return nbp;
}

std::unique_ptr<LabelPosition> FeaturePart::createCandidatePointOnSurface( PointSet *mapShape )
{
  double px, py;
  try
  {
    GEOSContextHandle_t geosctxt = QgsGeos::getGEOSHandler();
    geos::unique_ptr pointGeom( GEOSPointOnSurface_r( geosctxt, mapShape->geos() ) );
    if ( pointGeom )
    {
      const GEOSCoordSequence *coordSeq = GEOSGeom_getCoordSeq_r( geosctxt, pointGeom.get() );
      GEOSCoordSeq_getX_r( geosctxt, coordSeq, 0, &px );
      GEOSCoordSeq_getY_r( geosctxt, coordSeq, 0, &py );
    }
  }
  catch ( GEOSException &e )
  {
    QgsMessageLog::logMessage( QObject::tr( "Exception: %1" ).arg( e.what() ), QObject::tr( "GEOS" ) );
    return nullptr;
  }

  return qgis::make_unique< LabelPosition >( 0, px, py, getLabelWidth(), getLabelHeight(), 0.0, 0.0, this );
}

int FeaturePart::createCandidatesAtOrderedPositionsOverPoint( double x, double y, QList<LabelPosition *> &lPos, double angle )
{
  QVector< QgsPalLayerSettings::PredefinedPointPosition > positions = mLF->predefinedPositionOrder();
  double labelWidth = getLabelWidth();
  double labelHeight = getLabelHeight();
  double distanceToLabel = getLabelDistance();
  const QgsMargins &visualMargin = mLF->visualMargin();

  double symbolWidthOffset = ( mLF->offsetType() == QgsPalLayerSettings::FromSymbolBounds ? mLF->symbolSize().width() / 2.0 : 0.0 );
  double symbolHeightOffset = ( mLF->offsetType() == QgsPalLayerSettings::FromSymbolBounds ? mLF->symbolSize().height() / 2.0 : 0.0 );

  double cost = 0.0001;
  int i = 0;
  const auto constPositions = positions;
  for ( QgsPalLayerSettings::PredefinedPointPosition position : constPositions )
  {
    double alpha = 0.0;
    double deltaX = 0;
    double deltaY = 0;
    LabelPosition::Quadrant quadrant = LabelPosition::QuadrantAboveLeft;
    switch ( position )
    {
      case QgsPalLayerSettings::TopLeft:
        quadrant = LabelPosition::QuadrantAboveLeft;
        alpha = 3 * M_PI_4;
        deltaX = -labelWidth + visualMargin.right() - symbolWidthOffset;
        deltaY = -visualMargin.bottom() + symbolHeightOffset;
        break;

      case QgsPalLayerSettings::TopSlightlyLeft:
        quadrant = LabelPosition::QuadrantAboveRight; //right quadrant, so labels are left-aligned
        alpha = M_PI_2;
        deltaX = -labelWidth / 4.0 - visualMargin.left();
        deltaY = -visualMargin.bottom() + symbolHeightOffset;
        break;

      case QgsPalLayerSettings::TopMiddle:
        quadrant = LabelPosition::QuadrantAbove;
        alpha = M_PI_2;
        deltaX = -labelWidth / 2.0;
        deltaY = -visualMargin.bottom() + symbolHeightOffset;
        break;

      case QgsPalLayerSettings::TopSlightlyRight:
        quadrant = LabelPosition::QuadrantAboveLeft; //left quadrant, so labels are right-aligned
        alpha = M_PI_2;
        deltaX = -labelWidth * 3.0 / 4.0 + visualMargin.right();
        deltaY = -visualMargin.bottom() + symbolHeightOffset;
        break;

      case QgsPalLayerSettings::TopRight:
        quadrant = LabelPosition::QuadrantAboveRight;
        alpha = M_PI_4;
        deltaX = - visualMargin.left() + symbolWidthOffset;
        deltaY = -visualMargin.bottom() + symbolHeightOffset;
        break;

      case QgsPalLayerSettings::MiddleLeft:
        quadrant = LabelPosition::QuadrantLeft;
        alpha = M_PI;
        deltaX = -labelWidth + visualMargin.right() - symbolWidthOffset;
        deltaY = -labelHeight / 2.0;// TODO - should this be adjusted by visual margin??
        break;

      case QgsPalLayerSettings::MiddleRight:
        quadrant = LabelPosition::QuadrantRight;
        alpha = 0.0;
        deltaX = -visualMargin.left() + symbolWidthOffset;
        deltaY = -labelHeight / 2.0;// TODO - should this be adjusted by visual margin??
        break;

      case QgsPalLayerSettings::BottomLeft:
        quadrant = LabelPosition::QuadrantBelowLeft;
        alpha = 5 * M_PI_4;
        deltaX = -labelWidth + visualMargin.right() - symbolWidthOffset;
        deltaY = -labelHeight + visualMargin.top() - symbolHeightOffset;
        break;

      case QgsPalLayerSettings::BottomSlightlyLeft:
        quadrant = LabelPosition::QuadrantBelowRight; //right quadrant, so labels are left-aligned
        alpha = 3 * M_PI_2;
        deltaX = -labelWidth / 4.0 - visualMargin.left();
        deltaY = -labelHeight + visualMargin.top() - symbolHeightOffset;
        break;

      case QgsPalLayerSettings::BottomMiddle:
        quadrant = LabelPosition::QuadrantBelow;
        alpha = 3 * M_PI_2;
        deltaX = -labelWidth / 2.0;
        deltaY = -labelHeight + visualMargin.top() - symbolHeightOffset;
        break;

      case QgsPalLayerSettings::BottomSlightlyRight:
        quadrant = LabelPosition::QuadrantBelowLeft; //left quadrant, so labels are right-aligned
        alpha = 3 * M_PI_2;
        deltaX = -labelWidth * 3.0 / 4.0 + visualMargin.right();
        deltaY = -labelHeight + visualMargin.top() - symbolHeightOffset;
        break;

      case QgsPalLayerSettings::BottomRight:
        quadrant = LabelPosition::QuadrantBelowRight;
        alpha = 7 * M_PI_4;
        deltaX = -visualMargin.left() + symbolWidthOffset;
        deltaY = -labelHeight + visualMargin.top() - symbolHeightOffset;
        break;
    }

    //have bearing, distance - calculate reference point
    double referenceX = std::cos( alpha ) * distanceToLabel + x;
    double referenceY = std::sin( alpha ) * distanceToLabel + y;

    double labelX = referenceX + deltaX;
    double labelY = referenceY + deltaY;

    if ( ! mLF->permissibleZonePrepared() || GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), labelX, labelY, labelWidth, labelHeight, angle ) )
    {
      lPos << new LabelPosition( i, labelX, labelY, labelWidth, labelHeight, angle, cost, this, false, quadrant );
      //TODO - tweak
      cost += 0.001;
    }

    ++i;
  }

  return lPos.count();
}

int FeaturePart::createCandidatesAroundPoint( double x, double y, QList< LabelPosition * > &lPos, double angle )
{
  double labelWidth = getLabelWidth();
  double labelHeight = getLabelHeight();
  double distanceToLabel = getLabelDistance();

  int numberCandidates = mLF->layer()->pal->point_p;

  int icost = 0;
  int inc = 2;

  double candidateAngleIncrement = 2 * M_PI / numberCandidates; /* angle bw 2 pos */

  /* various angles */
  double a90  = M_PI_2;
  double a180 = M_PI;
  double a270 = a180 + a90;
  double a360 = 2 * M_PI;

  double gamma1, gamma2;

  if ( distanceToLabel > 0 )
  {
    gamma1 = std::atan2( labelHeight / 2, distanceToLabel + labelWidth / 2 );
    gamma2 = std::atan2( labelWidth / 2, distanceToLabel + labelHeight / 2 );
  }
  else
  {
    gamma1 = gamma2 = a90 / 3.0;
  }

  if ( gamma1 > a90 / 3.0 )
    gamma1 = a90 / 3.0;

  if ( gamma2 > a90 / 3.0 )
    gamma2 = a90 / 3.0;

  QList< LabelPosition * > candidates;

  int i;
  double angleToCandidate;
  for ( i = 0, angleToCandidate = M_PI_4; i < numberCandidates; i++, angleToCandidate += candidateAngleIncrement )
  {
    double labelX = x;
    double labelY = y;

    if ( angleToCandidate > a360 )
      angleToCandidate -= a360;

    LabelPosition::Quadrant quadrant = LabelPosition::QuadrantOver;

    if ( angleToCandidate < gamma1 || angleToCandidate > a360 - gamma1 )  // on the right
    {
      labelX += distanceToLabel;
      double iota = ( angleToCandidate + gamma1 );
      if ( iota > a360 - gamma1 )
        iota -= a360;

      //ly += -yrm/2.0 + tan(alpha)*(distlabel + xrm/2);
      labelY += -labelHeight + labelHeight * iota / ( 2 * gamma1 );

      quadrant = LabelPosition::QuadrantRight;
    }
    else if ( angleToCandidate < a90 - gamma2 )  // top-right
    {
      labelX += distanceToLabel * std::cos( angleToCandidate );
      labelY += distanceToLabel * std::sin( angleToCandidate );
      quadrant = LabelPosition::QuadrantAboveRight;
    }
    else if ( angleToCandidate < a90 + gamma2 ) // top
    {
      //lx += -xrm/2.0 - tan(alpha+a90)*(distlabel + yrm/2);
      labelX += -labelWidth * ( angleToCandidate - a90 + gamma2 ) / ( 2 * gamma2 );
      labelY += distanceToLabel;
      quadrant = LabelPosition::QuadrantAbove;
    }
    else if ( angleToCandidate < a180 - gamma1 )  // top left
    {
      labelX += distanceToLabel * std::cos( angleToCandidate ) - labelWidth;
      labelY += distanceToLabel * std::sin( angleToCandidate );
      quadrant = LabelPosition::QuadrantAboveLeft;
    }
    else if ( angleToCandidate < a180 + gamma1 ) // left
    {
      labelX += -distanceToLabel - labelWidth;
      //ly += -yrm/2.0 - tan(alpha)*(distlabel + xrm/2);
      labelY += - ( angleToCandidate - a180 + gamma1 ) * labelHeight / ( 2 * gamma1 );
      quadrant = LabelPosition::QuadrantLeft;
    }
    else if ( angleToCandidate < a270 - gamma2 ) // down - left
    {
      labelX += distanceToLabel * std::cos( angleToCandidate ) - labelWidth;
      labelY += distanceToLabel * std::sin( angleToCandidate ) - labelHeight;
      quadrant = LabelPosition::QuadrantBelowLeft;
    }
    else if ( angleToCandidate < a270 + gamma2 ) // down
    {
      labelY += -distanceToLabel - labelHeight;
      //lx += -xrm/2.0 + tan(alpha+a90)*(distlabel + yrm/2);
      labelX += -labelWidth + ( angleToCandidate - a270 + gamma2 ) * labelWidth / ( 2 * gamma2 );
      quadrant = LabelPosition::QuadrantBelow;
    }
    else if ( angleToCandidate < a360 ) // down - right
    {
      labelX += distanceToLabel * std::cos( angleToCandidate );
      labelY += distanceToLabel * std::sin( angleToCandidate ) - labelHeight;
      quadrant = LabelPosition::QuadrantBelowRight;
    }

    double cost;

    if ( numberCandidates == 1 )
      cost = 0.0001;
    else
      cost = 0.0001 + 0.0020 * double( icost ) / double( numberCandidates - 1 );


    if ( mLF->permissibleZonePrepared() )
    {
      if ( !GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), labelX, labelY, labelWidth, labelHeight, angle ) )
      {
        continue;
      }
    }

    candidates << new LabelPosition( i, labelX, labelY, labelWidth, labelHeight, angle, cost, this, false, quadrant );

    icost += inc;

    if ( icost == numberCandidates )
    {
      icost = numberCandidates - 1;
      inc = -2;
    }
    else if ( icost > numberCandidates )
    {
      icost = numberCandidates - 2;
      inc = -2;
    }

  }

  if ( !candidates.isEmpty() )
  {
    for ( int i = 0; i < candidates.count(); ++i )
    {
      lPos << candidates.at( i );
    }
  }

  return candidates.count();
}

int FeaturePart::createCandidatesAlongLine( QList< LabelPosition * > &lPos, PointSet *mapShape, bool allowOverrun )
{
  if ( allowOverrun )
  {
    double shapeLength = mapShape->length();
    if ( totalRepeats() > 1 && shapeLength < getLabelWidth() )
      return 0;
    else if ( shapeLength < getLabelWidth() - 2 * std::min( getLabelWidth(), mLF->overrunDistance() ) )
    {
      // label doesn't fit on this line, don't waste time trying to make candidates
      return 0;
    }
  }

  //prefer to label along straightish segments:
  int candidates = createCandidatesAlongLineNearStraightSegments( lPos, mapShape );

  if ( candidates < mLF->layer()->pal->line_p )
  {
    // but not enough candidates yet, so fallback to labeling near whole line's midpoint
    candidates = createCandidatesAlongLineNearMidpoint( lPos, mapShape, candidates > 0 ? 0.01 : 0.0 );
  }
  return candidates;
}

int FeaturePart::createCandidatesAlongLineNearStraightSegments( QList<LabelPosition *> &lPos, PointSet *mapShape )
{
  double labelWidth = getLabelWidth();
  double labelHeight = getLabelHeight();
  double distanceLineToLabel = getLabelDistance();
  LineArrangementFlags flags = mLF->arrangementFlags();
  if ( flags == 0 )
    flags = FLAG_ON_LINE; // default flag

  // first scan through the whole line and look for segments where the angle at a node is greater than 45 degrees - these form a "hard break" which labels shouldn't cross over
  QVector< int > extremeAngleNodes;
  PointSet *line = mapShape;
  int numberNodes = line->nbPoints;
  std::vector< double > &x = line->x;
  std::vector< double > &y = line->y;

  // closed line? if so, we need to handle the final node angle
  bool closedLine = qgsDoubleNear( x[0], x[ numberNodes - 1] ) && qgsDoubleNear( y[0], y[numberNodes - 1 ] );
  for ( int i = 1; i <= numberNodes - ( closedLine ? 1 : 2 ); ++i )
  {
    double x1 = x[i - 1];
    double x2 = x[i];
    double x3 = x[ i == numberNodes - 1 ? 1 : i + 1]; // wraparound for closed linestrings
    double y1 = y[i - 1];
    double y2 = y[i];
    double y3 = y[ i == numberNodes - 1 ? 1 : i + 1]; // wraparound for closed linestrings
    if ( qgsDoubleNear( y2, y3 ) && qgsDoubleNear( x2, x3 ) )
      continue;
    if ( qgsDoubleNear( y1, y2 ) && qgsDoubleNear( x1, x2 ) )
      continue;
    double vertexAngle = M_PI - ( std::atan2( y3 - y2, x3 - x2 ) - std::atan2( y2 - y1, x2 - x1 ) );
    vertexAngle = QgsGeometryUtils::normalizedAngle( vertexAngle );

    // extreme angles form more than 45 degree angle at a node - these are the ones we don't want labels to cross
    if ( vertexAngle < M_PI * 135.0 / 180.0 || vertexAngle > M_PI * 225.0 / 180.0 )
      extremeAngleNodes << i;
  }
  extremeAngleNodes << numberNodes - 1;

  if ( extremeAngleNodes.isEmpty() )
  {
    // no extreme angles - createCandidatesAlongLineNearMidpoint will be more appropriate
    return 0;
  }

  // calculate lengths of segments, and work out longest straight-ish segment
  double *segmentLengths = new double[ numberNodes - 1 ]; // segments lengths distance bw pt[i] && pt[i+1]
  double *distanceToSegment = new double[ numberNodes ]; // absolute distance bw pt[0] and pt[i] along the line
  double totalLineLength = 0.0;
  QVector< double > straightSegmentLengths;
  QVector< double > straightSegmentAngles;
  straightSegmentLengths.reserve( extremeAngleNodes.size() + 1 );
  straightSegmentAngles.reserve( extremeAngleNodes.size() + 1 );
  double currentStraightSegmentLength = 0;
  double longestSegmentLength = 0;
  int segmentIndex = 0;
  double segmentStartX = x[0];
  double segmentStartY = y[0];
  for ( int i = 0; i < numberNodes - 1; i++ )
  {
    if ( i == 0 )
      distanceToSegment[i] = 0;
    else
      distanceToSegment[i] = distanceToSegment[i - 1] + segmentLengths[i - 1];

    segmentLengths[i] = GeomFunction::dist_euc2d( x[i], y[i], x[i + 1], y[i + 1] );
    totalLineLength += segmentLengths[i];
    if ( extremeAngleNodes.contains( i ) )
    {
      // at an extreme angle node, so reset counters
      straightSegmentLengths << currentStraightSegmentLength;
      straightSegmentAngles << QgsGeometryUtils::normalizedAngle( std::atan2( y[i] - segmentStartY, x[i] - segmentStartX ) );
      longestSegmentLength = std::max( longestSegmentLength, currentStraightSegmentLength );
      segmentIndex++;
      currentStraightSegmentLength = 0;
      segmentStartX = x[i];
      segmentStartY = y[i];
    }
    currentStraightSegmentLength += segmentLengths[i];
  }
  distanceToSegment[line->nbPoints - 1] = totalLineLength;
  straightSegmentLengths << currentStraightSegmentLength;
  straightSegmentAngles << QgsGeometryUtils::normalizedAngle( std::atan2( y[numberNodes - 1] - segmentStartY, x[numberNodes - 1] - segmentStartX ) );
  longestSegmentLength = std::max( longestSegmentLength, currentStraightSegmentLength );
  double middleOfLine = totalLineLength / 2.0;

  if ( totalLineLength < labelWidth )
  {
    delete[] segmentLengths;
    delete[] distanceToSegment;
    return 0; //createCandidatesAlongLineNearMidpoint will be more appropriate
  }

  double lineStepDistance = ( totalLineLength - labelWidth ); // distance to move along line with each candidate
  lineStepDistance = std::min( std::min( labelHeight, labelWidth ), lineStepDistance / mLF->layer()->pal->line_p );

  double distanceToEndOfSegment = 0.0;
  int lastNodeInSegment = 0;
  // finally, loop through all these straight segments. For each we create candidates along the straight segment.
  for ( int i = 0; i < straightSegmentLengths.count(); ++i )
  {
    currentStraightSegmentLength = straightSegmentLengths.at( i );
    double currentSegmentAngle = straightSegmentAngles.at( i );
    lastNodeInSegment = extremeAngleNodes.at( i );
    double distanceToStartOfSegment = distanceToEndOfSegment;
    distanceToEndOfSegment = distanceToSegment[ lastNodeInSegment ];
    double distanceToCenterOfSegment = 0.5 * ( distanceToEndOfSegment + distanceToStartOfSegment );

    if ( currentStraightSegmentLength < labelWidth )
      // can't fit a label on here
      continue;

    double currentDistanceAlongLine = distanceToStartOfSegment;
    double candidateStartX, candidateStartY, candidateEndX, candidateEndY;
    double candidateLength = 0.0;
    double cost = 0.0;
    double angle = 0.0;
    double beta = 0.0;

    //calculate some cost penalties
    double segmentCost = 1.0 - ( distanceToEndOfSegment - distanceToStartOfSegment ) / longestSegmentLength; // 0 -> 1 (lower for longer segments)
    double segmentAngleCost = 1 - std::fabs( std::fmod( currentSegmentAngle, M_PI ) - M_PI_2 ) / M_PI_2; // 0 -> 1, lower for more horizontal segments

    while ( currentDistanceAlongLine + labelWidth < distanceToEndOfSegment )
    {
      // calculate positions along linestring corresponding to start and end of current label candidate
      line->getPointByDistance( segmentLengths, distanceToSegment, currentDistanceAlongLine, &candidateStartX, &candidateStartY );
      line->getPointByDistance( segmentLengths, distanceToSegment, currentDistanceAlongLine + labelWidth, &candidateEndX, &candidateEndY );

      candidateLength = std::sqrt( ( candidateEndX - candidateStartX ) * ( candidateEndX - candidateStartX ) + ( candidateEndY - candidateStartY ) * ( candidateEndY - candidateStartY ) );


      // LOTS OF DIFFERENT COSTS TO BALANCE HERE - feel free to tweak these, but please add a unit test
      // which covers the situation you are adjusting for (e.g., "given equal length lines, choose the more horizontal line")

      cost = candidateLength / labelWidth;
      if ( cost > 0.98 )
        cost = 0.0001;
      else
      {
        // jaggy line has a greater cost
        cost = ( 1 - cost ) / 100; // ranges from 0.0001 to 0.01 (however a cost 0.005 is already a lot!)
      }

      // penalize positions which are further from the straight segments's midpoint
      double labelCenter = currentDistanceAlongLine + labelWidth / 2.0;
      double costCenter = 2 * std::fabs( labelCenter - distanceToCenterOfSegment ) / ( distanceToEndOfSegment - distanceToStartOfSegment ); // 0 -> 1
      cost += costCenter * 0.0005;  // < 0, 0.0005 >

      if ( !closedLine )
      {
        // penalize positions which are further from absolute center of whole linestring
        // this only applies to non closed linestrings, since the middle of a closed linestring is effectively arbitrary
        // and irrelevant to labeling
        double costLineCenter = 2 * std::fabs( labelCenter - middleOfLine ) / totalLineLength; // 0 -> 1
        cost += costLineCenter * 0.0005;  // < 0, 0.0005 >
      }

      cost += segmentCost * 0.0005; // prefer labels on longer straight segments
      cost += segmentAngleCost * 0.0001; // prefer more horizontal segments, but this is less important than length considerations

      if ( qgsDoubleNear( candidateEndY, candidateStartY ) && qgsDoubleNear( candidateEndX, candidateStartX ) )
      {
        angle = 0.0;
      }
      else
        angle = std::atan2( candidateEndY - candidateStartY, candidateEndX - candidateStartX );

      beta = angle + M_PI_2;

      if ( mLF->layer()->arrangement() == QgsPalLayerSettings::Line )
      {
        // find out whether the line direction for this candidate is from right to left
        bool isRightToLeft = ( angle > M_PI_2 || angle <= -M_PI_2 );
        // meaning of above/below may be reversed if using map orientation and the line has right-to-left direction
        bool reversed = ( ( flags & FLAG_MAP_ORIENTATION ) ? isRightToLeft : false );
        bool aboveLine = ( !reversed && ( flags & FLAG_ABOVE_LINE ) ) || ( reversed && ( flags & FLAG_BELOW_LINE ) );
        bool belowLine = ( !reversed && ( flags & FLAG_BELOW_LINE ) ) || ( reversed && ( flags & FLAG_ABOVE_LINE ) );

        if ( belowLine )
        {
          if ( !mLF->permissibleZonePrepared() || GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), candidateStartX - std::cos( beta ) * ( distanceLineToLabel + labelHeight ), candidateStartY - std::sin( beta ) * ( distanceLineToLabel + labelHeight ), labelWidth, labelHeight, angle ) )
          {
            const double candidateCost = cost + ( reversed ? 0 : 0.001 );
            lPos.append( new LabelPosition( i, candidateStartX - std::cos( beta ) * ( distanceLineToLabel + labelHeight ), candidateStartY - std::sin( beta ) * ( distanceLineToLabel + labelHeight ), labelWidth, labelHeight, angle, candidateCost, this, isRightToLeft ) ); // Line
          }
        }
        if ( aboveLine )
        {
          if ( !mLF->permissibleZonePrepared() || GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), candidateStartX + std::cos( beta ) *distanceLineToLabel, candidateStartY + std::sin( beta ) *distanceLineToLabel, labelWidth, labelHeight, angle ) )
          {
            const double candidateCost = cost + ( !reversed ? 0 : 0.001 ); // no extra cost for above line placements
            lPos.append( new LabelPosition( i, candidateStartX + std::cos( beta ) *distanceLineToLabel, candidateStartY + std::sin( beta ) *distanceLineToLabel, labelWidth, labelHeight, angle, candidateCost, this, isRightToLeft ) ); // Line
          }
        }
        if ( flags & FLAG_ON_LINE )
        {
          if ( !mLF->permissibleZonePrepared() || GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), candidateStartX - labelHeight * std::cos( beta ) / 2, candidateStartY - labelHeight * std::sin( beta ) / 2, labelWidth, labelHeight, angle ) )
          {
            const double candidateCost = cost + 0.002;
            lPos.append( new LabelPosition( i, candidateStartX - labelHeight * std::cos( beta ) / 2, candidateStartY - labelHeight * std::sin( beta ) / 2, labelWidth, labelHeight, angle, candidateCost, this, isRightToLeft ) ); // Line
          }
        }
      }
      else if ( mLF->layer()->arrangement() == QgsPalLayerSettings::Horizontal )
      {
        lPos.append( new LabelPosition( i, candidateStartX - labelWidth / 2, candidateStartY - labelHeight / 2, labelWidth, labelHeight, 0, cost, this ) ); // Line
      }
      else
      {
        // an invalid arrangement?
      }

      currentDistanceAlongLine += lineStepDistance;
    }
  }

  delete[] segmentLengths;
  delete[] distanceToSegment;
  return lPos.size();
}

int FeaturePart::createCandidatesAlongLineNearMidpoint( QList<LabelPosition *> &lPos, PointSet *mapShape, double initialCost )
{
  double distanceLineToLabel = getLabelDistance();

  double labelWidth = getLabelWidth();
  double labelHeight = getLabelHeight();

  double angle;
  double cost;

  LineArrangementFlags flags = mLF->arrangementFlags();
  if ( flags == 0 )
    flags = FLAG_ON_LINE; // default flag

  QList<LabelPosition *> positions;

  PointSet *line = mapShape;
  int nbPoints = line->nbPoints;
  std::vector< double > &x = line->x;
  std::vector< double > &y = line->y;

  double *segmentLengths = new double[nbPoints - 1]; // segments lengths distance bw pt[i] && pt[i+1]
  double *distanceToSegment = new double[nbPoints]; // absolute distance bw pt[0] and pt[i] along the line

  double totalLineLength = 0.0; // line length
  for ( int i = 0; i < line->nbPoints - 1; i++ )
  {
    if ( i == 0 )
      distanceToSegment[i] = 0;
    else
      distanceToSegment[i] = distanceToSegment[i - 1] + segmentLengths[i - 1];

    segmentLengths[i] = GeomFunction::dist_euc2d( x[i], y[i], x[i + 1], y[i + 1] );
    totalLineLength += segmentLengths[i];
  }
  distanceToSegment[line->nbPoints - 1] = totalLineLength;

  double lineStepDistance = ( totalLineLength - labelWidth ); // distance to move along line with each candidate
  double currentDistanceAlongLine = 0;

  if ( totalLineLength > labelWidth )
  {
    lineStepDistance = std::min( std::min( labelHeight, labelWidth ), lineStepDistance / mLF->layer()->pal->line_p );
  }
  else if ( !line->isClosed() ) // line length < label width => centering label position
  {
    currentDistanceAlongLine = - ( labelWidth - totalLineLength ) / 2.0;
    lineStepDistance = -1;
    totalLineLength = labelWidth;
  }
  else
  {
    // closed line, not long enough for label => no candidates!
    currentDistanceAlongLine = std::numeric_limits< double >::max();
  }

  double candidateLength;
  double beta;
  double candidateStartX, candidateStartY, candidateEndX, candidateEndY;
  int i = 0;
  while ( currentDistanceAlongLine < totalLineLength - labelWidth )
  {
    // calculate positions along linestring corresponding to start and end of current label candidate
    line->getPointByDistance( segmentLengths, distanceToSegment, currentDistanceAlongLine, &candidateStartX, &candidateStartY );
    line->getPointByDistance( segmentLengths, distanceToSegment, currentDistanceAlongLine + labelWidth, &candidateEndX, &candidateEndY );

    if ( currentDistanceAlongLine < 0 )
    {
      // label is bigger than line, use whole available line
      candidateLength = std::sqrt( ( x[nbPoints - 1] - x[0] ) * ( x[nbPoints - 1] - x[0] )
                                   + ( y[nbPoints - 1] - y[0] ) * ( y[nbPoints - 1] - y[0] ) );
    }
    else
    {
      candidateLength = std::sqrt( ( candidateEndX - candidateStartX ) * ( candidateEndX - candidateStartX ) + ( candidateEndY - candidateStartY ) * ( candidateEndY - candidateStartY ) );
    }

    cost = candidateLength / labelWidth;
    if ( cost > 0.98 )
      cost = 0.0001;
    else
    {
      // jaggy line has a greater cost
      cost = ( 1 - cost ) / 100; // ranges from 0.0001 to 0.01 (however a cost 0.005 is already a lot!)
    }

    // penalize positions which are further from the line's midpoint
    double costCenter = std::fabs( totalLineLength / 2 - ( currentDistanceAlongLine + labelWidth / 2 ) ) / totalLineLength; // <0, 0.5>
    cost += costCenter / 1000;  // < 0, 0.0005 >
    cost += initialCost;

    if ( qgsDoubleNear( candidateEndY, candidateStartY ) && qgsDoubleNear( candidateEndX, candidateStartX ) )
    {
      angle = 0.0;
    }
    else
      angle = std::atan2( candidateEndY - candidateStartY, candidateEndX - candidateStartX );

    beta = angle + M_PI_2;

    if ( mLF->layer()->arrangement() == QgsPalLayerSettings::Line )
    {
      // find out whether the line direction for this candidate is from right to left
      bool isRightToLeft = ( angle > M_PI_2 || angle <= -M_PI_2 );
      // meaning of above/below may be reversed if using map orientation and the line has right-to-left direction
      bool reversed = ( ( flags & FLAG_MAP_ORIENTATION ) ? isRightToLeft : false );
      bool aboveLine = ( !reversed && ( flags & FLAG_ABOVE_LINE ) ) || ( reversed && ( flags & FLAG_BELOW_LINE ) );
      bool belowLine = ( !reversed && ( flags & FLAG_BELOW_LINE ) ) || ( reversed && ( flags & FLAG_ABOVE_LINE ) );

      if ( aboveLine )
      {
        if ( !mLF->permissibleZonePrepared() || GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), candidateStartX + std::cos( beta ) *distanceLineToLabel, candidateStartY + std::sin( beta ) *distanceLineToLabel, labelWidth, labelHeight, angle ) )
        {
          const double candidateCost = cost + ( !reversed ? 0 : 0.001 ); // no extra cost for above line placements
          positions.append( new LabelPosition( i, candidateStartX + std::cos( beta ) *distanceLineToLabel, candidateStartY + std::sin( beta ) *distanceLineToLabel, labelWidth, labelHeight, angle, candidateCost, this, isRightToLeft ) ); // Line
        }
      }
      if ( belowLine )
      {
        if ( !mLF->permissibleZonePrepared() || GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), candidateStartX - std::cos( beta ) * ( distanceLineToLabel + labelHeight ), candidateStartY - std::sin( beta ) * ( distanceLineToLabel + labelHeight ), labelWidth, labelHeight, angle ) )
        {
          const double candidateCost = cost + ( !reversed ? 0.001 : 0 );
          positions.append( new LabelPosition( i, candidateStartX - std::cos( beta ) * ( distanceLineToLabel + labelHeight ), candidateStartY - std::sin( beta ) * ( distanceLineToLabel + labelHeight ), labelWidth, labelHeight, angle, candidateCost, this, isRightToLeft ) ); // Line
        }
      }
      if ( flags & FLAG_ON_LINE )
      {
        if ( !mLF->permissibleZonePrepared() || GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), candidateStartX - labelHeight * std::cos( beta ) / 2, candidateStartY - labelHeight * std::sin( beta ) / 2, labelWidth, labelHeight, angle ) )
        {
          const double candidateCost = cost + 0.002;
          positions.append( new LabelPosition( i, candidateStartX - labelHeight * std::cos( beta ) / 2, candidateStartY - labelHeight * std::sin( beta ) / 2, labelWidth, labelHeight, angle, candidateCost, this, isRightToLeft ) ); // Line
        }
      }
    }
    else if ( mLF->layer()->arrangement() == QgsPalLayerSettings::Horizontal )
    {
      positions.append( new LabelPosition( i, candidateStartX - labelWidth / 2, candidateStartY - labelHeight / 2, labelWidth, labelHeight, 0, cost, this ) ); // Line
    }
    else
    {
      // an invalid arrangement?
    }

    currentDistanceAlongLine += lineStepDistance;

    i++;

    if ( lineStepDistance < 0 )
      break;
  }

  //delete line;

  delete[] segmentLengths;
  delete[] distanceToSegment;

  lPos.append( positions );
  return lPos.size();
}


LabelPosition *FeaturePart::curvedPlacementAtOffset( PointSet *path_positions, double *path_distances, int &orientation, const double offsetAlongLine, bool &reversed, bool &flip )
{
  double offsetAlongSegment = offsetAlongLine;
  int index = 1;
  // Find index of segment corresponding to starting offset
  while ( index < path_positions->nbPoints && offsetAlongSegment > path_distances[index] )
  {
    offsetAlongSegment -= path_distances[index];
    index += 1;
  }
  if ( index >= path_positions->nbPoints )
  {
    return nullptr;
  }

  LabelInfo *li = mLF->curvedLabelInfo();

  double string_height = li->label_height;

  const double segment_length = path_distances[index];
  if ( qgsDoubleNear( segment_length, 0.0 ) )
  {
    // Not allowed to place across on 0 length segments or discontinuities
    return nullptr;
  }

  if ( orientation == 0 )       // Must be map orientation
  {
    // Calculate the orientation based on the angle of the path segment under consideration

    double _distance = offsetAlongSegment;
    int endindex = index;

    double startLabelX = 0;
    double startLabelY = 0;
    double endLabelX = 0;
    double endLabelY = 0;
    for ( int i = 0; i < li->char_num; i++ )
    {
      LabelInfo::CharacterInfo &ci = li->char_info[i];
      double characterStartX, characterStartY;
      if ( !nextCharPosition( ci.width, path_distances[endindex], path_positions, endindex, _distance, characterStartX, characterStartY, endLabelX, endLabelY ) )
      {
        return nullptr;
      }
      if ( i == 0 )
      {
        startLabelX = characterStartX;
        startLabelY = characterStartY;
      }
    }

    // Determine the angle of the path segment under consideration
    double dx = endLabelX - startLabelX;
    double dy = endLabelY - startLabelY;
    const double lineAngle = std::atan2( -dy, dx ) * 180 / M_PI;

    bool isRightToLeft = ( lineAngle > 90 || lineAngle < -90 );
    reversed = isRightToLeft;
    orientation = isRightToLeft ? -1 : 1;
  }

  if ( !showUprightLabels() )
  {
    if ( orientation < 0 )
    {
      flip = true;   // Report to the caller, that the orientation is flipped
      reversed = !reversed;
      orientation = 1;
    }
  }

  LabelPosition *slp = nullptr;
  LabelPosition *slp_tmp = nullptr;

  double old_x = path_positions->x[index - 1];
  double old_y = path_positions->y[index - 1];

  double new_x = path_positions->x[index];
  double new_y = path_positions->y[index];

  double dx = new_x - old_x;
  double dy = new_y - old_y;

  double angle = std::atan2( -dy, dx );

  for ( int i = 0; i < li->char_num; i++ )
  {
    double last_character_angle = angle;

    // grab the next character according to the orientation
    LabelInfo::CharacterInfo &ci = ( orientation > 0 ? li->char_info[i] : li->char_info[li->char_num - i - 1] );
    if ( qgsDoubleNear( ci.width, 0.0 ) )
      // Certain scripts rely on zero-width character, skip those to prevent failure (see #15801)
      continue;

    double start_x, start_y, end_x, end_y;
    if ( !nextCharPosition( ci.width, path_distances[index], path_positions, index, offsetAlongSegment, start_x, start_y, end_x, end_y ) )
    {
      delete slp;
      return nullptr;
    }

    // Calculate angle from the start of the character to the end based on start_/end_ position
    angle = std::atan2( start_y - end_y, end_x - start_x );

    // Test last_character_angle vs angle
    // since our rendering angle has changed then check against our
    // max allowable angle change.
    double angle_delta = last_character_angle - angle;
    // normalise between -180 and 180
    while ( angle_delta > M_PI ) angle_delta -= 2 * M_PI;
    while ( angle_delta < -M_PI ) angle_delta += 2 * M_PI;
    if ( ( li->max_char_angle_inside > 0 && angle_delta > 0
           && angle_delta > li->max_char_angle_inside * ( M_PI / 180 ) )
         || ( li->max_char_angle_outside < 0 && angle_delta < 0
              && angle_delta < li->max_char_angle_outside * ( M_PI / 180 ) ) )
    {
      delete slp;
      return nullptr;
    }

    // Shift the character downwards since the draw position is specified at the baseline
    // and we're calculating the mean line here
    double dist = 0.9 * li->label_height / 2;
    if ( orientation < 0 )
    {
      dist = -dist;
      flip = true;
    }
    start_x += dist * std::cos( angle + M_PI_2 );
    start_y -= dist * std::sin( angle + M_PI_2 );

    double render_angle = angle;

    double render_x = start_x;
    double render_y = start_y;

    // Center the text on the line
    //render_x -= ((string_height/2.0) - 1.0)*math.cos(render_angle+math.pi/2)
    //render_y += ((string_height/2.0) - 1.0)*math.sin(render_angle+math.pi/2)

    if ( orientation < 0 )
    {
      // rotate in place
      render_x += ci.width * std::cos( render_angle ); //- (string_height-2)*sin(render_angle);
      render_y -= ci.width * std::sin( render_angle ); //+ (string_height-2)*cos(render_angle);
      render_angle += M_PI;
    }

    LabelPosition *tmp = new LabelPosition( 0, render_x /*- xBase*/, render_y /*- yBase*/, ci.width, string_height, -render_angle, 0.0001, this );
    tmp->setPartId( orientation > 0 ? i : li->char_num - i - 1 );
    if ( !slp )
      slp = tmp;
    else
      slp_tmp->setNextPart( tmp );
    slp_tmp = tmp;

    // Normalise to 0 <= angle < 2PI
    while ( render_angle >= 2 * M_PI ) render_angle -= 2 * M_PI;
    while ( render_angle < 0 ) render_angle += 2 * M_PI;

    if ( render_angle > M_PI_2 && render_angle < 1.5 * M_PI )
      slp->incrementUpsideDownCharCount();
  }

  return slp;
}

static LabelPosition *_createCurvedCandidate( LabelPosition *lp, double angle, double dist )
{
  LabelPosition *newLp = new LabelPosition( *lp );
  newLp->offsetPosition( dist * std::cos( angle + M_PI_2 ), dist * std::sin( angle + M_PI_2 ) );
  return newLp;
}

int FeaturePart::createCurvedCandidatesAlongLine( QList< LabelPosition * > &lPos, PointSet *mapShape, bool allowOverrun )
{
  LabelInfo *li = mLF->curvedLabelInfo();

  // label info must be present
  if ( !li || li->char_num == 0 )
    return 0;

  // TODO - we may need an explicit penalty for overhanging labels. Currently, they are penalized just because they
  // are further from the line center, so non-overhanding placements are picked where possible.

  double totalCharacterWidth = 0;
  for ( int i = 0; i < li->char_num; ++i )
    totalCharacterWidth += li->char_info[ i ].width;

  std::unique_ptr< PointSet > expanded;
  double shapeLength = mapShape->length();

  if ( totalRepeats() > 1 )
    allowOverrun = false;

  // label overrun should NEVER exceed the label length (or labels would sit off in space).
  // in fact, let's require that a minimum of 5% of the label text has to sit on the feature,
  // as we don't want a label sitting right at the start or end corner of a line
  double overrun = std::min( mLF->overrunDistance(), totalCharacterWidth * 0.95 );
  if ( totalCharacterWidth > shapeLength )
  {
    if ( !allowOverrun || shapeLength < totalCharacterWidth - 2 * overrun )
    {
      // label doesn't fit on this line, don't waste time trying to make candidates
      return 0;
    }
  }

  if ( allowOverrun && overrun > 0 )
  {
    // expand out line on either side to fit label
    expanded = mapShape->clone();
    expanded->extendLineByDistance( overrun, overrun, mLF->overrunSmoothDistance() );
    mapShape = expanded.get();
    shapeLength = mapShape->length();
  }

  // distance calculation
  std::unique_ptr< double [] > path_distances = qgis::make_unique<double[]>( mapShape->nbPoints );
  double total_distance = 0;
  double old_x = -1.0, old_y = -1.0;
  for ( int i = 0; i < mapShape->nbPoints; i++ )
  {
    if ( i == 0 )
      path_distances[i] = 0;
    else
      path_distances[i] = std::sqrt( std::pow( old_x - mapShape->x[i], 2 ) + std::pow( old_y - mapShape->y[i], 2 ) );
    old_x = mapShape->x[i];
    old_y = mapShape->y[i];

    total_distance += path_distances[i];
  }

  if ( qgsDoubleNear( total_distance, 0.0 ) )
  {
    return 0;
  }

  QLinkedList<LabelPosition *> positions;
  double delta = std::max( li->label_height / 6, total_distance / mLF->layer()->pal->line_p );

  pal::LineArrangementFlags flags = mLF->arrangementFlags();
  if ( flags == 0 )
    flags = FLAG_ON_LINE; // default flag

  // generate curved labels
  for ( double distanceAlongLineToStartCandidate = 0; distanceAlongLineToStartCandidate < total_distance; distanceAlongLineToStartCandidate += delta )
  {
    bool flip = false;
    // placements may need to be reversed if using map orientation and the line has right-to-left direction
    bool reversed = false;

    // an orientation of 0 means try both orientations and choose the best
    int orientation = 0;
    if ( !( flags & FLAG_MAP_ORIENTATION ) )
    {
      //... but if we are using line orientation flags, then we can only accept a single orientation,
      // as we need to ensure that the labels fall inside or outside the polyline or polygon (and not mixed)
      orientation = 1;
    }

    LabelPosition *slp = curvedPlacementAtOffset( mapShape, path_distances.get(), orientation, distanceAlongLineToStartCandidate, reversed, flip );
    if ( !slp )
      continue;

    // If we placed too many characters upside down
    if ( slp->upsideDownCharCount() >= li->char_num / 2.0 )
    {
      // if labels should be shown upright then retry with the opposite orientation
      if ( ( showUprightLabels() && !flip ) )
      {
        delete slp;
        orientation = -orientation;
        slp = curvedPlacementAtOffset( mapShape, path_distances.get(), orientation, distanceAlongLineToStartCandidate, reversed, flip );
      }
    }
    if ( !slp )
      continue;

    // evaluate cost
    double angle_diff = 0.0, angle_last = 0.0, diff;
    LabelPosition *tmp = slp;
    double sin_avg = 0, cos_avg = 0;
    while ( tmp )
    {
      if ( tmp != slp ) // not first?
      {
        diff = std::fabs( tmp->getAlpha() - angle_last );
        if ( diff > 2 * M_PI ) diff -= 2 * M_PI;
        diff = std::min( diff, 2 * M_PI - diff ); // difference 350 deg is actually just 10 deg...
        angle_diff += diff;
      }

      sin_avg += std::sin( tmp->getAlpha() );
      cos_avg += std::cos( tmp->getAlpha() );
      angle_last = tmp->getAlpha();
      tmp = tmp->getNextPart();
    }

    double angle_diff_avg = li->char_num > 1 ? ( angle_diff / ( li->char_num - 1 ) ) : 0; // <0, pi> but pi/8 is much already
    double cost = angle_diff_avg / 100; // <0, 0.031 > but usually <0, 0.003 >
    if ( cost < 0.0001 ) cost = 0.0001;

    // penalize positions which are further from the line's midpoint
    double labelCenter = distanceAlongLineToStartCandidate + getLabelWidth() / 2;
    double costCenter = std::fabs( total_distance / 2 - labelCenter ) / total_distance; // <0, 0.5>
    cost += costCenter / 100;  // < 0, 0.005 >
    slp->setCost( cost );

    // average angle is calculated with respect to periodicity of angles
    double angle_avg = std::atan2( sin_avg / li->char_num, cos_avg / li->char_num );
    bool localreversed = flip ? !reversed : reversed;
    // displacement - we loop through 3 times, generating above, online then below line placements successively
    for ( int i = 0; i <= 2; ++i )
    {
      LabelPosition *p = nullptr;
      if ( i == 0 && ( ( !localreversed && ( flags & FLAG_ABOVE_LINE ) ) || ( localreversed && ( flags & FLAG_BELOW_LINE ) ) ) )
        p = _createCurvedCandidate( slp, angle_avg, mLF->distLabel() + li->label_height / 2 );
      if ( i == 1 && flags & FLAG_ON_LINE )
      {
        p = _createCurvedCandidate( slp, angle_avg, 0 );
        p->setCost( p->cost() + 0.002 );
      }
      if ( i == 2 && ( ( !localreversed && ( flags & FLAG_BELOW_LINE ) ) || ( localreversed && ( flags & FLAG_ABOVE_LINE ) ) ) )
      {
        p = _createCurvedCandidate( slp, angle_avg, -li->label_height / 2 - mLF->distLabel() );
        p->setCost( p->cost() + 0.001 );
      }

      if ( p && mLF->permissibleZonePrepared() )
      {
        bool within = true;
        LabelPosition *currentPos = p;
        while ( within && currentPos )
        {
          within = GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), currentPos->getX(), currentPos->getY(), currentPos->getWidth(), currentPos->getHeight(), currentPos->getAlpha() );
          currentPos = currentPos->getNextPart();
        }
        if ( !within )
        {
          delete p;
          p = nullptr;
        }
      }

      if ( p )
        positions.append( p );
    }

    // delete original candidate
    delete slp;
  }

  int nbp = positions.size();
  for ( int i = 0; i < nbp; i++ )
  {
    lPos << positions.takeFirst();
  }

  return nbp;
}

/*
 *             seg 2
 *     pt3 ____________pt2
 *        ¦            ¦
 *        ¦            ¦
 * seg 3  ¦    BBOX    ¦ seg 1
 *        ¦            ¦
 *        ¦____________¦
 *     pt0    seg 0    pt1
 *
 */

int FeaturePart::createCandidatesForPolygon( QList< LabelPosition *> &lPos, PointSet *mapShape )
{
  int i;
  int j;

  double labelWidth = getLabelWidth();
  double labelHeight = getLabelHeight();

  QLinkedList<PointSet *> shapes_toProcess;
  QLinkedList<PointSet *> shapes_final;

  mapShape->parent = nullptr;

  shapes_toProcess.append( mapShape );

  splitPolygons( shapes_toProcess, shapes_final, labelWidth, labelHeight );

  int nbp;

  if ( !shapes_final.isEmpty() )
  {
    QLinkedList<LabelPosition *> positions;

    int id = 0; // ids for candidates
    double dlx, dly; // delta from label center and bottom-left corner
    double alpha = 0.0; // rotation for the label
    double px, py;
    double dx;
    double dy;
    int bbid;
    double beta;
    double diago = std::sqrt( labelWidth * labelWidth / 4.0 + labelHeight * labelHeight / 4 );
    double rx, ry;
    CHullBox **boxes = new CHullBox*[shapes_final.size()];
    j = 0;

    // Compute bounding box foreach finalShape
    while ( !shapes_final.isEmpty() )
    {
      PointSet *shape = shapes_final.takeFirst();
      boxes[j] = shape->compute_chull_bbox();

      if ( shape->parent )
        delete shape;

      j++;
    }

    //dx = dy = min( yrm, xrm ) / 2;
    dx = labelWidth / 2.0;
    dy = labelHeight / 2.0;

    int numTry = 0;

    //fit in polygon only mode slows down calculation a lot, so if it's enabled
    //then use a smaller limit for number of iterations
    int maxTry = mLF->permissibleZonePrepared() ? 7 : 10;

    do
    {
      for ( bbid = 0; bbid < j; bbid++ )
      {
        CHullBox *box = boxes[bbid];

        if ( ( box->length * box->width ) > ( xmax - xmin ) * ( ymax - ymin ) * 5 )
        {
          // Very Large BBOX (should never occur)
          continue;
        }

        if ( mLF->layer()->arrangement() == QgsPalLayerSettings::Horizontal && mLF->permissibleZonePrepared() )
        {
          //check width/height of bbox is sufficient for label
          if ( mLF->permissibleZone().boundingBox().width() < labelWidth ||
               mLF->permissibleZone().boundingBox().height() < labelHeight )
          {
            //no way label can fit in this box, skip it
            continue;
          }
        }

        bool enoughPlace = false;
        if ( mLF->layer()->arrangement() == QgsPalLayerSettings::Free )
        {
          enoughPlace = true;
          px = ( box->x[0] + box->x[2] ) / 2 - labelWidth;
          py = ( box->y[0] + box->y[2] ) / 2 - labelHeight;
          int i, j;

          // Virtual label: center on bbox center, label size = 2x original size
          // alpha = 0.
          // If all corner are in bbox then place candidates horizontaly
          for ( rx = px, i = 0; i < 2; rx = rx + 2 * labelWidth, i++ )
          {
            for ( ry = py, j = 0; j < 2; ry = ry + 2 * labelHeight, j++ )
            {
              if ( !mapShape->containsPoint( rx, ry ) )
              {
                enoughPlace = false;
                break;
              }
            }
            if ( !enoughPlace )
            {
              break;
            }
          }

        } // arrangement== FREE ?

        if ( mLF->layer()->arrangement() == QgsPalLayerSettings::Horizontal || enoughPlace )
        {
          alpha = 0.0; // HORIZ
        }
        else if ( box->length > 1.5 * labelWidth && box->width > 1.5 * labelWidth )
        {
          if ( box->alpha <= M_PI_4 )
          {
            alpha = box->alpha;
          }
          else
          {
            alpha = box->alpha - M_PI_2;
          }
        }
        else if ( box->length > box->width )
        {
          alpha = box->alpha - M_PI_2;
        }
        else
        {
          alpha = box->alpha;
        }

        beta  = std::atan2( labelHeight, labelWidth ) + alpha;


        //alpha = box->alpha;

        // delta from label center and down-left corner
        dlx = std::cos( beta ) * diago;
        dly = std::sin( beta ) * diago;

        double px0, py0;

        px0 = box->width / 2.0;
        py0 = box->length / 2.0;

        px0 -= std::ceil( px0 / dx ) * dx;
        py0 -= std::ceil( py0 / dy ) * dy;

        for ( px = px0; px <= box->width; px += dx )
        {
          for ( py = py0; py <= box->length; py += dy )
          {

            rx = std::cos( box->alpha ) * px + std::cos( box->alpha - M_PI_2 ) * py;
            ry = std::sin( box->alpha ) * px + std::sin( box->alpha - M_PI_2 ) * py;

            rx += box->x[0];
            ry += box->y[0];

            bool candidateAcceptable = ( mLF->permissibleZonePrepared()
                                         ? GeomFunction::containsCandidate( mLF->permissibleZonePrepared(), rx - dlx, ry - dly, labelWidth, labelHeight, alpha )
                                         : mapShape->containsPoint( rx, ry ) );
            if ( candidateAcceptable )
            {
              // cost is set to minimal value, evaluated later
              positions.append( new LabelPosition( id++, rx - dlx, ry - dly, labelWidth, labelHeight, alpha, 0.0001, this ) ); // Polygon
            }
          }
        }
      } // forall box

      nbp = positions.size();
      if ( nbp == 0 )
      {
        dx /= 2;
        dy /= 2;
        numTry++;
      }
    }
    while ( nbp == 0 && numTry < maxTry );

    nbp = positions.size();

    for ( i = 0; i < nbp; i++ )
    {
      lPos << positions.takeFirst();
    }

    for ( bbid = 0; bbid < j; bbid++ )
    {
      delete boxes[bbid];
    }

    delete[] boxes;
  }
  else
  {
    nbp = 0;
  }

  return nbp;
}

QList<LabelPosition *> FeaturePart::createCandidates( const GEOSPreparedGeometry *mapBoundary,
    PointSet *mapShape, RTree<LabelPosition *, double, 2, double> *candidates )
{
  QList<LabelPosition *> lPos;
  double angle = mLF->hasFixedAngle() ? mLF->fixedAngle() : 0.0;

  if ( mLF->hasFixedPosition() )
  {
    lPos << new LabelPosition( 0, mLF->fixedPosition().x(), mLF->fixedPosition().y(), getLabelWidth(), getLabelHeight(), angle, 0.0, this );
  }
  else
  {
    switch ( type )
    {
      case GEOS_POINT:
        if ( mLF->layer()->arrangement() == QgsPalLayerSettings::OrderedPositionsAroundPoint )
          createCandidatesAtOrderedPositionsOverPoint( x[0], y[0], lPos, angle );
        else if ( mLF->layer()->arrangement() == QgsPalLayerSettings::OverPoint || mLF->hasFixedQuadrant() )
          createCandidatesOverPoint( x[0], y[0], lPos, angle );
        else
          createCandidatesAroundPoint( x[0], y[0], lPos, angle );
        break;
      case GEOS_LINESTRING:
        if ( mLF->layer()->isCurved() )
          createCurvedCandidatesAlongLine( lPos, mapShape, true );
        else
          createCandidatesAlongLine( lPos, mapShape, true );
        break;

      case GEOS_POLYGON:
        switch ( mLF->layer()->arrangement() )
        {
          case QgsPalLayerSettings::AroundPoint:
          case QgsPalLayerSettings::OverPoint:
            double cx, cy;
            mapShape->getCentroid( cx, cy, mLF->layer()->centroidInside() );
            if ( mLF->layer()->arrangement() == QgsPalLayerSettings::OverPoint )
              createCandidatesOverPoint( cx, cy, lPos, angle );
            else
              createCandidatesAroundPoint( cx, cy, lPos, angle );
            break;
          case QgsPalLayerSettings::Line:
            createCandidatesAlongLine( lPos, mapShape );
            break;
          case QgsPalLayerSettings::PerimeterCurved:
            createCurvedCandidatesAlongLine( lPos, mapShape );
            break;
          default:
            createCandidatesForPolygon( lPos, mapShape );
            break;
        }
    }
  }

  // purge candidates that are outside the bbox

  QMutableListIterator< LabelPosition *> i( lPos );
  while ( i.hasNext() )
  {
    LabelPosition *pos = i.next();
    bool outside = false;

    if ( mLF->layer()->pal->getShowPartial() )
      outside = !pos->intersects( mapBoundary );
    else
      outside = !pos->within( mapBoundary );
    if ( outside )
    {
      i.remove();
      delete pos;
    }
    else   // this one is OK
    {
      pos->insertIntoIndex( candidates );
    }
  }

  std::sort( lPos.begin(), lPos.end(), CostCalculator::candidateSortGrow );
  return lPos;
}

void FeaturePart::addSizePenalty( int nbp, QList< LabelPosition * > &lPos, double bbx[4], double bby[4] )
{
  if ( !mGeos )
    createGeosGeom();

  GEOSContextHandle_t ctxt = QgsGeos::getGEOSHandler();
  int geomType = GEOSGeomTypeId_r( ctxt, mGeos );

  double sizeCost = 0;
  if ( geomType == GEOS_LINESTRING )
  {
    double length;
    try
    {
      if ( GEOSLength_r( ctxt, mGeos, &length ) != 1 )
        return; // failed to calculate length
    }
    catch ( GEOSException &e )
    {
      QgsMessageLog::logMessage( QObject::tr( "Exception: %1" ).arg( e.what() ), QObject::tr( "GEOS" ) );
      return;
    }
    double bbox_length = std::max( bbx[2] - bbx[0], bby[2] - bby[0] );
    if ( length >= bbox_length / 4 )
      return; // the line is longer than quarter of height or width - don't penalize it

    sizeCost = 1 - ( length / ( bbox_length / 4 ) ); // < 0,1 >
  }
  else if ( geomType == GEOS_POLYGON )
  {
    double area;
    try
    {
      if ( GEOSArea_r( ctxt, mGeos, &area ) != 1 )
        return;
    }
    catch ( GEOSException &e )
    {
      QgsMessageLog::logMessage( QObject::tr( "Exception: %1" ).arg( e.what() ), QObject::tr( "GEOS" ) );
      return;
    }
    double bbox_area = ( bbx[2] - bbx[0] ) * ( bby[2] - bby[0] );
    if ( area >= bbox_area / 16 )
      return; // covers more than 1/16 of our view - don't penalize it

    sizeCost = 1 - ( area / ( bbox_area / 16 ) ); // < 0, 1 >
  }
  else
    return; // no size penalty for points

  // apply the penalty
  for ( int i = 0; i < nbp; i++ )
  {
    lPos.at( i )->setCost( lPos.at( i )->cost() + sizeCost / 100 );
  }
}

bool FeaturePart::isConnected( FeaturePart *p2 )
{
  if ( !p2->mGeos )
    p2->createGeosGeom();

  try
  {
    return ( GEOSPreparedTouches_r( QgsGeos::getGEOSHandler(), preparedGeom(), p2->mGeos ) == 1 );
  }
  catch ( GEOSException &e )
  {
    QgsMessageLog::logMessage( QObject::tr( "Exception: %1" ).arg( e.what() ), QObject::tr( "GEOS" ) );
    return false;
  }
}

bool FeaturePart::mergeWithFeaturePart( FeaturePart *other )
{
  if ( !mGeos )
    createGeosGeom();
  if ( !other->mGeos )
    other->createGeosGeom();

  GEOSContextHandle_t ctxt = QgsGeos::getGEOSHandler();
  try
  {
    GEOSGeometry *g1 = GEOSGeom_clone_r( ctxt, mGeos );
    GEOSGeometry *g2 = GEOSGeom_clone_r( ctxt, other->mGeos );
    GEOSGeometry *geoms[2] = { g1, g2 };
    geos::unique_ptr g( GEOSGeom_createCollection_r( ctxt, GEOS_MULTILINESTRING, geoms, 2 ) );
    geos::unique_ptr gTmp( GEOSLineMerge_r( ctxt, g.get() ) );

    if ( GEOSGeomTypeId_r( ctxt, gTmp.get() ) != GEOS_LINESTRING )
    {
      // sometimes it's not possible to merge lines (e.g. they don't touch at endpoints)
      return false;
    }
    invalidateGeos();

    // set up new geometry
    mGeos = gTmp.release();
    mOwnsGeom = true;

    deleteCoords();
    qDeleteAll( mHoles );
    mHoles.clear();
    extractCoords( mGeos );
    return true;
  }
  catch ( GEOSException &e )
  {
    QgsMessageLog::logMessage( QObject::tr( "Exception: %1" ).arg( e.what() ), QObject::tr( "GEOS" ) );
    return false;
  }
}

double FeaturePart::calculatePriority() const
{
  if ( mLF->alwaysShow() )
  {
    //if feature is set to always show, bump the priority up by orders of magnitude
    //so that other feature's labels are unlikely to be placed over the label for this feature
    //(negative numbers due to how pal::extract calculates inactive cost)
    return -0.2;
  }

  return mLF->priority() >= 0 ? mLF->priority() : mLF->layer()->priority();
}

bool FeaturePart::showUprightLabels() const
{
  bool uprightLabel = false;

  switch ( mLF->layer()->upsidedownLabels() )
  {
    case Layer::Upright:
      uprightLabel = true;
      break;
    case Layer::ShowDefined:
      // upright only dynamic labels
      if ( !hasFixedRotation() || ( !hasFixedPosition() && fixedAngle() == 0.0 ) )
      {
        uprightLabel = true;
      }
      break;
    case Layer::ShowAll:
      break;
    default:
      uprightLabel = true;
  }
  return uprightLabel;
}

bool FeaturePart::nextCharPosition( double charWidth, double segmentLength, PointSet *path_positions, int &index, double &currentDistanceAlongSegment,
                                    double &characterStartX, double &characterStartY, double &characterEndX, double &characterEndY ) const
{
  // Coordinates this character will start at
  if ( qgsDoubleNear( segmentLength, 0.0 ) )
  {
    // Not allowed to place across on 0 length segments or discontinuities
    return false;
  }

  double segmentStartX = path_positions->x[index - 1];
  double segmentStartY = path_positions->y[index - 1];

  double segmentEndX = path_positions->x[index];
  double segmentEndY = path_positions->y[index];

  double segmentDx = segmentEndX - segmentStartX;
  double segmentDy = segmentEndY - segmentStartY;

  characterStartX = segmentStartX + segmentDx * currentDistanceAlongSegment / segmentLength;
  characterStartY = segmentStartY + segmentDy * currentDistanceAlongSegment / segmentLength;

  // Coordinates this character ends at, calculated below
  characterEndX = 0;
  characterEndY = 0;

  if ( segmentLength - currentDistanceAlongSegment >= charWidth )
  {
    // if the distance remaining in this segment is enough, we just go further along the segment
    currentDistanceAlongSegment += charWidth;
    characterEndX = segmentStartX + segmentDx * currentDistanceAlongSegment / segmentLength;
    characterEndY = segmentStartY + segmentDy * currentDistanceAlongSegment / segmentLength;
  }
  else
  {
    // If there isn't enough distance left on this segment
    // then we need to search until we find the line segment that ends further than ci.width away
    do
    {
      segmentStartX = segmentEndX;
      segmentStartY = segmentEndY;
      index++;
      if ( index >= path_positions->nbPoints ) // Bail out if we run off the end of the shape
      {
        return false;
      }
      segmentEndX = path_positions->x[index];
      segmentEndY = path_positions->y[index];
    }
    while ( std::sqrt( std::pow( characterStartX - segmentEndX, 2 ) + std::pow( characterStartY - segmentEndY, 2 ) ) < charWidth ); // Distance from start_ to new_

    // Calculate the position to place the end of the character on
    GeomFunction::findLineCircleIntersection( characterStartX, characterStartY, charWidth, segmentStartX, segmentStartY, segmentEndX, segmentEndY, characterEndX, characterEndY );

    // Need to calculate distance on the new segment
    currentDistanceAlongSegment = std::sqrt( std::pow( segmentStartX - characterEndX, 2 ) + std::pow( segmentStartY - characterEndY, 2 ) );
  }
  return true;
}
