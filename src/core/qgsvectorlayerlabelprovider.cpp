/***************************************************************************
  qgsvectorlayerlabelprovider.cpp
  --------------------------------------
  Date                 : September 2015
  Copyright            : (C) 2015 by Martin Dobias
  Email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsvectorlayerlabelprovider.h"

#include "qgsgeometry.h"
#include "qgslabelsearchtree.h"
#include "qgspallabeling.h"
#include "qgstextlabelfeature.h"
#include "qgsvectorlayer.h"
#include "qgsvectorlayerfeatureiterator.h"
#include "qgsrenderer.h"
#include "qgspolygon.h"
#include "qgslinestring.h"
#include "qgsmultipolygon.h"
#include "qgslogger.h"
#include "qgsexpressioncontextutils.h"

#include "feature.h"
#include "labelposition.h"
#include "callouts/qgscallout.h"

#include <QPicture>

using namespace pal;

QgsVectorLayerLabelProvider::QgsVectorLayerLabelProvider( QgsVectorLayer *layer, const QString &providerId, bool withFeatureLoop, const QgsPalLayerSettings *settings, const QString &layerName )
  : QgsAbstractLabelProvider( layer, providerId )
  , mSettings( settings ? * settings : QgsPalLayerSettings() ) // TODO: all providers should have valid settings?
  , mLayerGeometryType( layer->geometryType() )
  , mRenderer( layer->renderer() )
  , mFields( layer->fields() )
  , mCrs( layer->crs() )
{
  mName = layerName.isEmpty() ? layer->id() : layerName;

  if ( withFeatureLoop )
  {
    mSource = qgis::make_unique<QgsVectorLayerFeatureSource>( layer );
  }

  init();
}

void QgsVectorLayerLabelProvider::init()
{
  mPlacement = mSettings.placement;
  mFlags = Flags();
  if ( mSettings.drawLabels )
    mFlags |= DrawLabels;
  if ( mSettings.displayAll )
    mFlags |= DrawAllLabels;
  if ( mSettings.mergeLines && !mSettings.addDirectionSymbol )
    mFlags |= MergeConnectedLines;
  if ( mSettings.centroidInside )
    mFlags |= CentroidMustBeInside;

  mPriority = 1 - mSettings.priority / 10.0; // convert 0..10 --> 1..0

  if ( mLayerGeometryType == QgsWkbTypes::PointGeometry && mRenderer )
  {
    //override obstacle type to treat any intersection of a label with the point symbol as a high cost conflict
    mObstacleType = QgsPalLayerSettings::PolygonWhole;
  }
  else
  {
    mObstacleType = mSettings.obstacleType;
  }

  mUpsidedownLabels = mSettings.upsidedownLabels;
}


QgsVectorLayerLabelProvider::~QgsVectorLayerLabelProvider()
{
  qDeleteAll( mLabels );
}


bool QgsVectorLayerLabelProvider::prepare( QgsRenderContext &context, QSet<QString> &attributeNames )
{
  const QgsMapSettings &mapSettings = mEngine->mapSettings();

  return mSettings.prepare( context, attributeNames, mFields, mapSettings, mCrs );
}

void QgsVectorLayerLabelProvider::startRender( QgsRenderContext &context )
{
  QgsAbstractLabelProvider::startRender( context );
  mSettings.startRender( context );
}

void QgsVectorLayerLabelProvider::stopRender( QgsRenderContext &context )
{
  QgsAbstractLabelProvider::stopRender( context );
  mSettings.stopRender( context );
}

QList<QgsLabelFeature *> QgsVectorLayerLabelProvider::labelFeatures( QgsRenderContext &ctx )
{
  if ( !mSource )
  {
    // we have created the provider with "own feature loop" == false
    // so it is assumed that prepare() has been already called followed by registerFeature() calls
    return mLabels;
  }

  QSet<QString> attrNames;
  if ( !prepare( ctx, attrNames ) )
    return QList<QgsLabelFeature *>();

  if ( mRenderer )
    mRenderer->startRender( ctx, mFields );

  QgsRectangle layerExtent = ctx.extent();
  if ( mSettings.ct.isValid() && !mSettings.ct.isShortCircuited() )
    layerExtent = mSettings.ct.transformBoundingBox( ctx.extent(), QgsCoordinateTransform::ReverseTransform );

  QgsFeatureRequest request;
  request.setFilterRect( layerExtent );
  request.setSubsetOfAttributes( attrNames, mFields );
  QgsFeatureIterator fit = mSource->getFeatures( request );

  QgsExpressionContextScope *symbolScope = new QgsExpressionContextScope();
  ctx.expressionContext().appendScope( symbolScope );
  QgsFeature fet;
  while ( fit.nextFeature( fet ) )
  {
    QgsGeometry obstacleGeometry;
    std::unique_ptr< QgsSymbol > symbol;
    if ( mRenderer )
    {
      QgsSymbolList symbols = mRenderer->originalSymbolsForFeature( fet, ctx );
      if ( !symbols.isEmpty() && fet.geometry().type() == QgsWkbTypes::PointGeometry )
      {
        //point feature, use symbol bounds as obstacle
        obstacleGeometry = QgsVectorLayerLabelProvider::getPointObstacleGeometry( fet, ctx, symbols );
      }
      if ( !symbols.isEmpty() )
      {
        symbol.reset( symbols.at( 0 )->clone() );
        symbolScope = QgsExpressionContextUtils::updateSymbolScope( symbol.get(), symbolScope );
      }
    }
    ctx.expressionContext().setFeature( fet );
    registerFeature( fet, ctx, obstacleGeometry, symbol.release() );
  }

  if ( ctx.expressionContext().lastScope() == symbolScope )
    delete ctx.expressionContext().popScope();

  if ( mRenderer )
    mRenderer->stopRender( ctx );

  return mLabels;
}

void QgsVectorLayerLabelProvider::registerFeature( const QgsFeature &feature, QgsRenderContext &context, const QgsGeometry &obstacleGeometry, const QgsSymbol *symbol )
{
  QgsLabelFeature *label = nullptr;

  mSettings.registerFeature( feature, context, &label, obstacleGeometry, symbol );
  if ( label )
    mLabels << label;
}

QgsGeometry QgsVectorLayerLabelProvider::getPointObstacleGeometry( QgsFeature &fet, QgsRenderContext &context, const QgsSymbolList &symbols )
{
  if ( !fet.hasGeometry() || fet.geometry().type() != QgsWkbTypes::PointGeometry )
    return QgsGeometry();

  bool isMultiPoint = fet.geometry().constGet()->nCoordinates() > 1;
  std::unique_ptr< QgsAbstractGeometry > obstacleGeom;
  if ( isMultiPoint )
    obstacleGeom = qgis::make_unique< QgsMultiPolygon >();

  // for each point
  for ( int i = 0; i < fet.geometry().constGet()->nCoordinates(); ++i )
  {
    QRectF bounds;
    QgsPoint p = fet.geometry().constGet()->vertexAt( QgsVertexId( i, 0, 0 ) );
    double x = p.x();
    double y = p.y();
    double z = 0; // dummy variable for coordinate transforms

    //transform point to pixels
    if ( context.coordinateTransform().isValid() )
    {
      try
      {
        context.coordinateTransform().transformInPlace( x, y, z );
      }
      catch ( QgsCsException & )
      {
        return QgsGeometry();
      }
    }
    context.mapToPixel().transformInPlace( x, y );

    QPointF pt( x, y );
    const auto constSymbols = symbols;
    for ( QgsSymbol *symbol : constSymbols )
    {
      if ( symbol->type() == QgsSymbol::Marker )
      {
        if ( bounds.isValid() )
          bounds = bounds.united( static_cast< QgsMarkerSymbol * >( symbol )->bounds( pt, context, fet ) );
        else
          bounds = static_cast< QgsMarkerSymbol * >( symbol )->bounds( pt, context, fet );
      }
    }

    //convert bounds to a geometry
    QVector< double > bX;
    bX << bounds.left() << bounds.right() << bounds.right() << bounds.left();
    QVector< double > bY;
    bY << bounds.top() << bounds.top() << bounds.bottom() << bounds.bottom();
    std::unique_ptr< QgsLineString > boundLineString = qgis::make_unique< QgsLineString >( bX, bY );

    //then transform back to map units
    //TODO - remove when labeling is refactored to use screen units
    for ( int i = 0; i < boundLineString->numPoints(); ++i )
    {
      QgsPointXY point = context.mapToPixel().toMapCoordinates( static_cast<int>( boundLineString->xAt( i ) ),
                         static_cast<int>( boundLineString->yAt( i ) ) );
      boundLineString->setXAt( i, point.x() );
      boundLineString->setYAt( i, point.y() );
    }
    if ( context.coordinateTransform().isValid() )
    {
      try
      {
        boundLineString->transform( context.coordinateTransform(), QgsCoordinateTransform::ReverseTransform );
      }
      catch ( QgsCsException & )
      {
        return QgsGeometry();
      }
    }
    boundLineString->close();

    if ( context.coordinateTransform().isValid() )
    {
      // coordinate transforms may have resulted in nan coordinates - if so, strip these out
      boundLineString->filterVertices( []( const QgsPoint & point )->bool
      {
        return std::isfinite( point.x() ) && std::isfinite( point.y() );
      } );
      if ( !boundLineString->isRing() )
        return QgsGeometry();
    }

    std::unique_ptr< QgsPolygon > obstaclePolygon = qgis::make_unique< QgsPolygon >();
    obstaclePolygon->setExteriorRing( boundLineString.release() );

    if ( isMultiPoint )
    {
      static_cast<QgsMultiPolygon *>( obstacleGeom.get() )->addGeometry( obstaclePolygon.release() );
    }
    else
    {
      obstacleGeom = std::move( obstaclePolygon );
    }
  }

  return QgsGeometry( std::move( obstacleGeom ) );
}

void QgsVectorLayerLabelProvider::drawLabelBackground( QgsRenderContext &context, LabelPosition *label ) const
{
  if ( !mSettings.drawLabels )
    return;

  // render callout
  if ( mSettings.callout() && mSettings.callout()->drawOrder() == QgsCallout::OrderBelowAllLabels )
  {
    drawCallout( context, label );
  }
}

void QgsVectorLayerLabelProvider::drawCallout( QgsRenderContext &context, pal::LabelPosition *label ) const
{
  bool enabled = mSettings.callout()->enabled();
  if ( mSettings.dataDefinedProperties().isActive( QgsPalLayerSettings::CalloutDraw ) )
  {
    context.expressionContext().setOriginalValueVariable( enabled );
    enabled = mSettings.dataDefinedProperties().valueAsBool( QgsPalLayerSettings::CalloutDraw, context.expressionContext(), enabled );
  }
  if ( enabled )
  {
    QgsMapToPixel xform = context.mapToPixel();
    xform.setMapRotation( 0, 0, 0 );
    QPointF outPt = xform.transform( label->getX(), label->getY() ).toQPointF();
    QgsPointXY outPt2 = xform.transform( label->getX() + label->getWidth(), label->getY() + label->getHeight() );
    QRectF rect( outPt.x(), outPt.y(), outPt2.x() - outPt.x(), outPt2.y() - outPt.y() );

    QgsGeometry g( QgsGeos::fromGeos( label->getFeaturePart()->feature()->geometry() ) );
    g.transform( xform.transform() );
    QgsCallout::QgsCalloutContext calloutContext;
    calloutContext.allFeaturePartsLabeled = label->getFeaturePart()->feature()->labelAllParts();
    mSettings.callout()->render( context, rect, label->getAlpha() * 180 / M_PI, g, calloutContext );
  }
}

void QgsVectorLayerLabelProvider::drawLabel( QgsRenderContext &context, pal::LabelPosition *label ) const
{
  if ( !mSettings.drawLabels )
    return;

  QgsTextLabelFeature *lf = dynamic_cast<QgsTextLabelFeature *>( label->getFeaturePart()->feature() );

  // Copy to temp, editable layer settings
  // these settings will be changed by any data defined values, then used for rendering label components
  // settings may be adjusted during rendering of components
  QgsPalLayerSettings tmpLyr( mSettings );

  // apply any previously applied data defined settings for the label
  const QMap< QgsPalLayerSettings::Property, QVariant > &ddValues = lf->dataDefinedValues();

  //font
  QFont dFont = lf->definedFont();
  QgsDebugMsgLevel( QStringLiteral( "PAL font tmpLyr: %1, Style: %2" ).arg( tmpLyr.format().font().toString(), tmpLyr.format().font().styleName() ), 4 );
  QgsDebugMsgLevel( QStringLiteral( "PAL font definedFont: %1, Style: %2" ).arg( dFont.toString(), dFont.styleName() ), 4 );

  QgsTextFormat format = tmpLyr.format();
  format.setFont( dFont );

  // size has already been calculated and stored in the defined font - this calculated size
  // is in pixels
  format.setSize( dFont.pixelSize() );
  format.setSizeUnit( QgsUnitTypes::RenderPixels );
  tmpLyr.setFormat( format );

  if ( tmpLyr.multilineAlign == QgsPalLayerSettings::MultiFollowPlacement )
  {
    //calculate font alignment based on label quadrant
    switch ( label->getQuadrant() )
    {
      case LabelPosition::QuadrantAboveLeft:
      case LabelPosition::QuadrantLeft:
      case LabelPosition::QuadrantBelowLeft:
        tmpLyr.multilineAlign = QgsPalLayerSettings::MultiRight;
        break;
      case LabelPosition::QuadrantAbove:
      case LabelPosition::QuadrantOver:
      case LabelPosition::QuadrantBelow:
        tmpLyr.multilineAlign = QgsPalLayerSettings::MultiCenter;
        break;
      case LabelPosition::QuadrantAboveRight:
      case LabelPosition::QuadrantRight:
      case LabelPosition::QuadrantBelowRight:
        tmpLyr.multilineAlign = QgsPalLayerSettings::MultiLeft;
        break;
    }
  }

  // update tmpLyr with any data defined text style values
  QgsPalLabeling::dataDefinedTextStyle( tmpLyr, ddValues );

  // update tmpLyr with any data defined text buffer values
  QgsPalLabeling::dataDefinedTextBuffer( tmpLyr, ddValues );

  // update tmpLyr with any data defined text formatting values
  QgsPalLabeling::dataDefinedTextFormatting( tmpLyr, ddValues );

  // update tmpLyr with any data defined shape background values
  QgsPalLabeling::dataDefinedShapeBackground( tmpLyr, ddValues );

  // update tmpLyr with any data defined drop shadow values
  QgsPalLabeling::dataDefinedDropShadow( tmpLyr, ddValues );

  // Render the components of a label in reverse order
  //   (backgrounds -> text)

  // render callout
  if ( mSettings.callout() && mSettings.callout()->drawOrder() == QgsCallout::OrderBelowIndividualLabels )
  {
    drawCallout( context, label );
  }

  if ( tmpLyr.format().shadow().enabled() && tmpLyr.format().shadow().shadowPlacement() == QgsTextShadowSettings::ShadowLowest )
  {
    QgsTextFormat format = tmpLyr.format();

    if ( tmpLyr.format().background().enabled() && tmpLyr.format().background().type() != QgsTextBackgroundSettings::ShapeMarkerSymbol ) // background shadows not compatible with marker symbol backgrounds
    {
      format.shadow().setShadowPlacement( QgsTextShadowSettings::ShadowShape );
    }
    else if ( tmpLyr.format().buffer().enabled() )
    {
      format.shadow().setShadowPlacement( QgsTextShadowSettings::ShadowBuffer );
    }
    else
    {
      format.shadow().setShadowPlacement( QgsTextShadowSettings::ShadowText );
    }

    tmpLyr.setFormat( format );
  }

  if ( tmpLyr.format().background().enabled() )
  {
    drawLabelPrivate( label, context, tmpLyr, QgsTextRenderer::Background );
  }

  if ( tmpLyr.format().buffer().enabled() )
  {
    drawLabelPrivate( label, context, tmpLyr, QgsTextRenderer::Buffer );
  }

  drawLabelPrivate( label, context, tmpLyr, QgsTextRenderer::Text );

  // add to the results
  QString labeltext = label->getFeaturePart()->feature()->labelText();
  mEngine->results()->mLabelSearchTree->insertLabel( label, label->getFeaturePart()->featureId(), mLayerId, labeltext, dFont, false, lf->hasFixedPosition(), mProviderId );
}

void QgsVectorLayerLabelProvider::drawUnplacedLabel( QgsRenderContext &context, LabelPosition *label ) const
{
  if ( !mSettings.drawLabels )
    return;

  QgsTextLabelFeature *lf = dynamic_cast<QgsTextLabelFeature *>( label->getFeaturePart()->feature() );

  QgsPalLayerSettings tmpLyr( mSettings );
  QgsTextFormat format = tmpLyr.format();
  format.setColor( mEngine->engineSettings().unplacedLabelColor() );
  tmpLyr.setFormat( format );
  drawLabelPrivate( label, context, tmpLyr, QgsTextRenderer::Text );

  // add to the results
  QString labeltext = label->getFeaturePart()->feature()->labelText();
  mEngine->results()->mLabelSearchTree->insertLabel( label, label->getFeaturePart()->featureId(), mLayerId, labeltext, tmpLyr.format().font(), false, lf->hasFixedPosition(), mProviderId, true );
}

void QgsVectorLayerLabelProvider::drawLabelPrivate( pal::LabelPosition *label, QgsRenderContext &context, QgsPalLayerSettings &tmpLyr, QgsTextRenderer::TextPart drawType, double dpiRatio ) const
{
  // NOTE: this is repeatedly called for multi-part labels
  QPainter *painter = context.painter();

  // features are pre-rotated but not scaled/translated,
  // so we only disable rotation here. Ideally, they'd be
  // also pre-scaled/translated, as suggested here:
  // https://github.com/qgis/QGIS/issues/20071
  QgsMapToPixel xform = context.mapToPixel();
  xform.setMapRotation( 0, 0, 0 );

  QPointF outPt = xform.transform( label->getX(), label->getY() ).toQPointF();

  if ( mEngine->engineSettings().testFlag( QgsLabelingEngineSettings::DrawLabelRectOnly ) )  // TODO: this should get directly to labeling engine
  {
    //debugging rect
    if ( drawType != QgsTextRenderer::Text )
      return;

    QgsPointXY outPt2 = xform.transform( label->getX() + label->getWidth(), label->getY() + label->getHeight() );
    QRectF rect( 0, 0, outPt2.x() - outPt.x(), outPt2.y() - outPt.y() );
    painter->save();
    painter->setRenderHint( QPainter::Antialiasing, false );
    painter->translate( QPointF( outPt.x(), outPt.y() ) );
    painter->rotate( -label->getAlpha() * 180 / M_PI );

    if ( label->conflictsWithObstacle() )
    {
      painter->setBrush( QColor( 255, 0, 0, 100 ) );
      painter->setPen( QColor( 255, 0, 0, 150 ) );
    }
    else
    {
      painter->setBrush( QColor( 0, 255, 0, 100 ) );
      painter->setPen( QColor( 0, 255, 0, 150 ) );
    }

    painter->drawRect( rect );
    painter->restore();

    if ( label->getNextPart() )
      drawLabelPrivate( label->getNextPart(), context, tmpLyr, drawType, dpiRatio );

    return;
  }

  QgsTextRenderer::Component component;
  component.dpiRatio = dpiRatio;
  component.origin = outPt;
  component.rotation = label->getAlpha();



  if ( drawType == QgsTextRenderer::Background )
  {
    // get rotated label's center point
    QPointF centerPt( outPt );
    QgsPointXY outPt2 = xform.transform( label->getX() + label->getWidth() / 2,
                                         label->getY() + label->getHeight() / 2 );

    double xc = outPt2.x() - outPt.x();
    double yc = outPt2.y() - outPt.y();

    double angle = -component.rotation;
    double xd = xc * std::cos( angle ) - yc * std::sin( angle );
    double yd = xc * std::sin( angle ) + yc * std::cos( angle );

    centerPt.setX( centerPt.x() + xd );
    centerPt.setY( centerPt.y() + yd );

    component.center = centerPt;

    // convert label size to render units
    double labelWidthPx = context.convertToPainterUnits( label->getWidth(), QgsUnitTypes::RenderMapUnits, QgsMapUnitScale() );
    double labelHeightPx = context.convertToPainterUnits( label->getHeight(), QgsUnitTypes::RenderMapUnits, QgsMapUnitScale() );

    component.size = QSizeF( labelWidthPx, labelHeightPx );

    QgsTextRenderer::drawBackground( context, component, tmpLyr.format(), QStringList(), QgsTextRenderer::Label );
  }

  else if ( drawType == QgsTextRenderer::Buffer
            || drawType == QgsTextRenderer::Text )
  {

    // TODO: optimize access :)
    QgsTextLabelFeature *lf = static_cast<QgsTextLabelFeature *>( label->getFeaturePart()->feature() );
    QString txt = lf->text( label->getPartId() );
    QFontMetricsF *labelfm = lf->labelFontMetrics();

    //add the direction symbol if needed
    if ( !txt.isEmpty() && tmpLyr.placement == QgsPalLayerSettings::Line &&
         tmpLyr.addDirectionSymbol )
    {
      bool prependSymb = false;
      QString symb = tmpLyr.rightDirectionSymbol;

      if ( label->getReversed() )
      {
        prependSymb = true;
        symb = tmpLyr.leftDirectionSymbol;
      }

      if ( tmpLyr.reverseDirectionSymbol )
      {
        if ( symb == tmpLyr.rightDirectionSymbol )
        {
          prependSymb = true;
          symb = tmpLyr.leftDirectionSymbol;
        }
        else
        {
          prependSymb = false;
          symb = tmpLyr.rightDirectionSymbol;
        }
      }

      if ( tmpLyr.placeDirectionSymbol == QgsPalLayerSettings::SymbolAbove )
      {
        prependSymb = true;
        symb = symb + QStringLiteral( "\n" );
      }
      else if ( tmpLyr.placeDirectionSymbol == QgsPalLayerSettings::SymbolBelow )
      {
        prependSymb = false;
        symb = QStringLiteral( "\n" ) + symb;
      }

      if ( prependSymb )
      {
        txt.prepend( symb );
      }
      else
      {
        txt.append( symb );
      }
    }

    //QgsDebugMsgLevel( "drawLabel " + txt, 4 );
    QStringList multiLineList = QgsPalLabeling::splitToLines( txt, tmpLyr.wrapChar, tmpLyr.autoWrapLength, tmpLyr.useMaxLineLengthForAutoWrap );

    QgsTextRenderer::HAlignment hAlign = QgsTextRenderer::AlignLeft;
    if ( tmpLyr.multilineAlign == QgsPalLayerSettings::MultiCenter )
      hAlign = QgsTextRenderer::AlignCenter;
    else if ( tmpLyr.multilineAlign == QgsPalLayerSettings::MultiRight )
      hAlign = QgsTextRenderer::AlignRight;

    QgsTextRenderer::Component component;
    component.origin = outPt;
    component.rotation = label->getAlpha();

    QgsTextRenderer::drawTextInternal( drawType, context, tmpLyr.format(), component, multiLineList, labelfm,
                                       hAlign, QgsTextRenderer::Label );

  }

  // NOTE: this used to be within above multi-line loop block, at end. (a mistake since 2010? [LS])
  if ( label->getNextPart() )
    drawLabelPrivate( label->getNextPart(), context, tmpLyr, drawType, dpiRatio );
}
