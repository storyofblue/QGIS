/***************************************************************************
     testqgsgeometrychecks.cpp
     --------------------------------------
    Date                 : September 2017
    Copyright            : (C) 2017 Sandro Mani
    Email                : manisandro at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgstest.h"
#include "qgsfeature.h"
#include "qgsfeaturepool.h"
#include "qgsvectorlayer.h"

#include "qgsgeometryanglecheck.h"
#include "qgsgeometryareacheck.h"
#include "qgsgeometrycontainedcheck.h"
#include "qgsgeometrydanglecheck.h"
#include "qgsgeometrydegeneratepolygoncheck.h"
#include "qgsgeometryduplicatecheck.h"
#include "qgsgeometryduplicatenodescheck.h"
#include "qgsgeometryfollowboundariescheck.h"
#include "qgsgeometrygapcheck.h"
#include "qgsgeometryholecheck.h"
#include "qgsgeometrymissingvertexcheck.h"
#include "qgsgeometrylineintersectioncheck.h"
#include "qgsgeometrylinelayerintersectioncheck.h"
#include "qgsgeometrymultipartcheck.h"
#include "qgsgeometryoverlapcheck.h"
#include "qgsgeometrypointcoveredbylinecheck.h"
#include "qgsgeometrypointinpolygoncheck.h"
#include "qgsgeometrysegmentlengthcheck.h"
#include "qgsgeometryselfcontactcheck.h"
#include "qgsgeometryselfintersectioncheck.h"
#include "qgsgeometrysliverpolygoncheck.h"
#include "qgsvectordataproviderfeaturepool.h"
#include "qgsmultilinestring.h"
#include "qgslinestring.h"
#include "qgsproject.h"
#include "qgsfeedback.h"

#include "qgsgeometrytypecheck.h"

class TestQgsGeometryChecks: public QObject
{
    Q_OBJECT
  private:
    struct Change
    {
      QString layerId;
      QgsFeatureId fid;
      QgsGeometryCheck::ChangeWhat what;
      QgsGeometryCheck::ChangeType type;
      QgsVertexId vidx;
    };
    double layerToMapUnits( const QgsMapLayer *layer, const QgsCoordinateReferenceSystem &mapCrs ) const;
    QgsFeaturePool *createFeaturePool( QgsVectorLayer *layer, bool selectedOnly = false ) const;
    QPair<QgsGeometryCheckContext *, QMap<QString, QgsFeaturePool *> > createTestContext( QTemporaryDir &tempDir, QMap<QString, QString> &layers, const QgsCoordinateReferenceSystem &mapCrs = QgsCoordinateReferenceSystem( "EPSG:4326" ), double prec = 8 ) const;
    void cleanupTestContext( QPair<QgsGeometryCheckContext *, QMap<QString, QgsFeaturePool *> > ctx ) const;
    void listErrors( const QList<QgsGeometryCheckError *> &checkErrors, const QStringList &messages ) const;
    QList<QgsGeometryCheckError *> searchCheckErrors( const QList<QgsGeometryCheckError *> &checkErrors, const QString &layerId, const QgsFeatureId &featureId = -1, const QgsPointXY &pos = QgsPointXY(), const QgsVertexId &vid = QgsVertexId(), const QVariant &value = QVariant(), double tol = 1E-4 ) const;
    bool fixCheckError( QMap<QString, QgsFeaturePool *> featurePools, QgsGeometryCheckError *error, int method, const QgsGeometryCheckError::Status &expectedStatus, const QVector<Change> &expectedChanges, const QMap<QString, int> &mergeAttr = QMap<QString, int>() );
    QgsGeometryCheck::Changes change2changes( const Change &change ) const;

  private slots:
    // will be called before the first testfunction is executed.
    void initTestCase();
    // will be called after the last testfunction is executed.
    void cleanupTestCase();

  private slots:
    void testAngleCheck();
    void testAreaCheck();
    void testContainedCheck();
    void testDangleCheck();
    void testDegeneratePolygonCheck();
    void testDuplicateCheck();
    void testDuplicateNodesCheck();
    void testFollowBoundariesCheck();
    void testGapCheck();
    void testMissingVertexCheck();
    void testHoleCheck();
    void testLineIntersectionCheck();
    void testLineLayerIntersectionCheck();
    void testMultipartCheck();
    void testOverlapCheck();
    void testOverlapCheckNoMaxArea();
    void testPointCoveredByLineCheck();
    void testPointInPolygonCheck();
    void testSegmentLengthCheck();
    void testSelfContactCheck();
    void testSelfIntersectionCheck();
    void testSliverPolygonCheck();
};

void TestQgsGeometryChecks::initTestCase()
{
  QgsApplication::initQgis();
}

void TestQgsGeometryChecks::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

///////////////////////////////////////////////////////////////////////////////

void TestQgsGeometryChecks::testAngleCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "minAngle", 15 );

  QgsGeometryAngleCheck check( testContext.first, configuration );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;
  QList<QgsGeometryCheckError *> errs2;

  QCOMPARE( checkErrors.size(), 8 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["line_layer.shp"], 0, QgsPointXY( -0.2225,  0.5526 ), QgsVertexId( 0, 0, 3 ), 10.5865 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 0, QgsPointXY( -0.94996, 0.99967 ), QgsVertexId( 1, 0, 1 ), 8.3161 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 2, QgsPointXY( -0.4547, -0.3059 ), QgsVertexId( 0, 0, 1 ), 5.4165 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 2, QgsPointXY( -0.7594, -0.1971 ), QgsVertexId( 0, 0, 2 ), 12.5288 ).size() == 1 );
  QVERIFY( ( errs2 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 0, QgsPointXY( 0.2402, 1.0786 ), QgsVertexId( 0, 0, 1 ), 13.5140 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 1, QgsPointXY( 0.6960, 0.5908 ), QgsVertexId( 0, 0, 0 ), 7.0556 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 1, QgsPointXY( 0.98690, 0.55699 ), QgsVertexId( 1, 0, 5 ), 7.7351 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 11, QgsPointXY( -0.3186, 1.6734 ), QgsVertexId( 0, 0, 1 ), 3.5092 ).size() == 1 );

  // Test fixes
  QgsFeature f;
  int n1, n2;

  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  n1 = f.geometry().constGet()->vertexCount( errs1[0]->vidx().part, errs1[0]->vidx().ring );
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryAngleCheck::DeleteNode, QgsGeometryCheckError::StatusFixed,
  {{errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeRemoved, errs1[0]->vidx()}} ) );
  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  n2 = f.geometry().constGet()->vertexCount( errs1[0]->vidx().part, errs1[0]->vidx().ring );
  QCOMPARE( n1, n2 + 1 );

  testContext.second[errs2[0]->layerId()]->getFeature( errs2[0]->featureId(), f );
  n1 = f.geometry().constGet()->vertexCount( errs2[0]->vidx().part, errs2[0]->vidx().ring );
  QVERIFY( fixCheckError( testContext.second,  errs2[0],
                          QgsGeometryAngleCheck::DeleteNode, QgsGeometryCheckError::StatusFixed,
  {{errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeRemoved, errs2[0]->vidx()}} ) );
  testContext.second[errs2[0]->layerId()]->getFeature( errs2[0]->featureId(), f );
  n2 = f.geometry().constGet()->vertexCount( errs2[0]->vidx().part, errs2[0]->vidx().ring );
  QCOMPARE( n1, n2 + 1 );

  // Test change tracking
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()} ) ) );
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeChanged, QgsVertexId()} ) ) );
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( errs2[0]->vidx().part )} ) ) );
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeChanged, QgsVertexId( errs2[0]->vidx().part )} ) ) );
  QVERIFY( errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( errs2[0]->vidx().part + 1 )} ) ) );
  QVERIFY( errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeChanged, QgsVertexId( errs2[0]->vidx().part + 1 )} ) ) );
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeRemoved, QgsVertexId( errs2[0]->vidx().part, errs2[0]->vidx().ring )} ) ) );
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeChanged, QgsVertexId( errs2[0]->vidx().part, errs2[0]->vidx().ring )} ) ) );
  QVERIFY( errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeRemoved, QgsVertexId( errs2[0]->vidx().part, errs2[0]->vidx().ring + 1 )} ) ) );
  QVERIFY( errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeChanged, QgsVertexId( errs2[0]->vidx().part, errs2[0]->vidx().ring + 1 )} ) ) );
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeRemoved, errs2[0]->vidx()} ) ) );
  QVERIFY( !errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeChanged, errs2[0]->vidx()} ) ) );
  QVERIFY( errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeRemoved, QgsVertexId( errs2[0]->vidx().part, errs2[0]->vidx().ring, errs2[0]->vidx().vertex + 1 )} ) ) );
  QVERIFY( errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeChanged, QgsVertexId( errs2[0]->vidx().part, errs2[0]->vidx().ring, errs2[0]->vidx().vertex + 1 )} ) ) );
  QgsVertexId oldVidx = errs2[0]->vidx();
  QVERIFY( errs2[0]->handleChanges( change2changes( {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeRemoved, QgsVertexId( errs2[0]->vidx().part, errs2[0]->vidx().ring, errs2[0]->vidx().vertex - 1 )} ) ) );
  QVERIFY( errs2[0]->vidx().vertex == oldVidx.vertex - 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testAreaCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "areaThreshold", 0.04 );

  QgsGeometryAreaCheck check( testContext.first, configuration );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;
  QList<QgsGeometryCheckError *> errs2;
  QList<QgsGeometryCheckError *> errs3;
  QList<QgsGeometryCheckError *> errs4;

  QCOMPARE( checkErrors.size(), 8 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 1, QgsPointXY( 1.0068, 0.3635 ), QgsVertexId( 1 ), 0.0105 ).size() == 1 );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 2, QgsPointXY( 0.9739, 1.0983 ), QgsVertexId( 0 ), 0.0141 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 6, QgsPointXY( 0.9968, 1.7584 ), QgsVertexId( 0 ), 0 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 11, QgsPointXY( -0.2941, 1.4614 ), QgsVertexId( 0 ), 0.0031 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 12, QgsPointXY( 0.2229, 0.2306 ), QgsVertexId( 0 ), -0.0079 ).size() == 1 ); // This is a polygon with a self-intersection hence the incorrect negative area
  QVERIFY( ( errs2 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 13, QgsPointXY( 0.5026, 3.0267 ), QgsVertexId( 0 ), 0.0013 ) ).size() == 1 );
  QVERIFY( ( errs3 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 20, QgsPointXY( 0.4230, 3.4688 ), QgsVertexId( 0 ), 0.0013 ) ).size() == 1 );
  QVERIFY( ( errs4 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 24, QgsPointXY( 1.4186, 3.0905 ), QgsVertexId( 0 ), 0.0013 ) ).size() == 1 );

  // Test fixes
  QgsFeature f;
  bool valid;

  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryAreaCheck::Delete, QgsGeometryCheckError::StatusFixed,
  {{errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()}} ) );
  valid = testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  QVERIFY( !valid );

  // Try merging a small geometry by longest edge, largest area and common value
  testContext.second[layers["polygon_layer.shp"]]->getFeature( 15, f );
  double area15 = f.geometry().area();
  QVERIFY( fixCheckError( testContext.second,  errs2[0],
                          QgsGeometryAreaCheck::MergeLargestArea, QgsGeometryCheckError::StatusFixed,
  {
    {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()},
    {layers["polygon_layer.shp"], 15, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0 )},
    {layers["polygon_layer.shp"], 15, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 0 )}
  } ) );
  testContext.second[layers["polygon_layer.shp"]]->getFeature( 15, f );
  QVERIFY( f.geometry().area() > area15 );
  valid = testContext.second[errs2[0]->layerId()]->getFeature( errs2[0]->featureId(), f );
  QVERIFY( !valid );

  testContext.second[layers["polygon_layer.shp"]]->getFeature( 18, f );
  double area18 = f.geometry().area();
  QVERIFY( fixCheckError( testContext.second,  errs3[0],
                          QgsGeometryAreaCheck::MergeLongestEdge, QgsGeometryCheckError::StatusFixed,
  {
    {errs3[0]->layerId(), errs3[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()},
    {layers["polygon_layer.shp"], 18, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0 )},
    {layers["polygon_layer.shp"], 18, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 0 )}
  } ) );
  testContext.second[layers["polygon_layer.shp"]]->getFeature( 18, f );
  QVERIFY( f.geometry().area() > area18 );
  valid = testContext.second[errs3[0]->layerId()]->getFeature( errs3[0]->featureId(), f );
  QVERIFY( !valid );

  testContext.second[layers["polygon_layer.shp"]]->getFeature( 21, f );
  double area21 = f.geometry().area();
  QMap<QString, int> mergeIdx;
  mergeIdx.insert( layers["polygon_layer.shp"], 1 ); // 1: attribute "attr"
  QVERIFY( fixCheckError( testContext.second,  errs4[0],
                          QgsGeometryAreaCheck::MergeIdenticalAttribute, QgsGeometryCheckError::StatusFixed,
  {
    {errs4[0]->layerId(), errs4[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()},
    {layers["polygon_layer.shp"], 21, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0 )},
    {layers["polygon_layer.shp"], 21, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 0 )}
  }, mergeIdx ) );
  testContext.second[layers["polygon_layer.shp"]]->getFeature( 21, f );
  QVERIFY( f.geometry().area() > area21 );
  valid = testContext.second[errs4[0]->layerId()]->getFeature( errs4[0]->featureId(), f );
  QVERIFY( !valid );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testContainedCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryContainedCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 4 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"], 4, QgsPointXY(), QgsVertexId(), QVariant( "polygon_layer.shp:5" ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"], 5, QgsPointXY(), QgsVertexId(), QVariant( "polygon_layer.shp:0" ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 3, QgsPointXY(), QgsVertexId(), QVariant( "polygon_layer.shp:0" ) ).size() == 1 );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 3, QgsPointXY(), QgsVertexId(), QVariant( "polygon_layer.shp:0" ) ) ).size() == 1 );
  QVERIFY( messages.contains( "Contained check failed for (polygon_layer.shp:1): the geometry is invalid" ) );

  // Test fixes
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryContainedCheck::Delete, QgsGeometryCheckError::StatusFixed,
  {{errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()}} ) );
  QgsFeature f;
  bool valid = testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  QVERIFY( !valid );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testDangleCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryDangleCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 6 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"] ).isEmpty() );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["line_layer.shp"], 0, QgsPointXY( -0.7558, 0.7648 ), QgsVertexId( 1, 0, 0 ) ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 2, QgsPointXY( -0.7787, -0.2237 ), QgsVertexId( 0, 0, 0 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 3, QgsPointXY( -0.2326, 0.9537 ), QgsVertexId( 0, 0, 0 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 3, QgsPointXY( 0.0715, 0.7830 ), QgsVertexId( 0, 0, 3 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 8, QgsPointXY( -1.5974, 0.5237 ), QgsVertexId( 0, 0, 0 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 8, QgsPointXY( -0.9315, 0.3146 ), QgsVertexId( 0, 0, 5 ) ).size() == 1 );

  // Test change tracking
  QgsVertexId oldVidx = errs1[0]->vidx();
  QVERIFY( errs1[0]->handleChanges( change2changes( {errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0 )} ) ) );
  QVERIFY( errs1[0]->vidx().part == oldVidx.part - 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testDegeneratePolygonCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryDegeneratePolygonCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 6, QgsPointXY(), QgsVertexId( 0, 0 ) ) ).size() == 1 );

  // Test fixes
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryDegeneratePolygonCheck::DeleteRing, QgsGeometryCheckError::StatusFixed,
  {{errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()}} ) );
  QgsFeature f;
  bool valid = testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  QVERIFY( !valid );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testDuplicateCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryDuplicateCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 3 );
  QVERIFY(
    searchCheckErrors( checkErrors, layers["point_layer.shp"], 6, QgsPoint(), QgsVertexId(), QVariant( "point_layer.shp:2" ) ).size() == 1
    || searchCheckErrors( checkErrors, layers["point_layer.shp"], 2, QgsPoint(), QgsVertexId(), QVariant( "point_layer.shp:6" ) ).size() == 1 );
  QVERIFY(
    searchCheckErrors( checkErrors, layers["line_layer.shp"], 4, QgsPoint(), QgsVertexId(), QVariant( "line_layer.shp:7" ) ).size() == 1
    || searchCheckErrors( checkErrors, layers["line_layer.shp"], 7, QgsPoint(), QgsVertexId(), QVariant( "line_layer.shp:4" ) ).size() == 1 );
  QVERIFY(
    ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 8, QgsPoint(), QgsVertexId(), QVariant( "polygon_layer.shp:7" ) ) ).size() == 1
    || ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 7, QgsPoint(), QgsVertexId(), QVariant( "polygon_layer.shp:8" ) ) ).size() == 1 );

  // Test fixes
  QgsGeometryDuplicateCheckError *dupErr = static_cast<QgsGeometryDuplicateCheckError *>( errs1[0] );
  QString dup1LayerId = dupErr->duplicates().firstKey();
  QgsFeatureId dup1Fid = dupErr->duplicates()[dup1LayerId][0];
  QVERIFY( fixCheckError( testContext.second,  dupErr,
                          QgsGeometryDuplicateCheck::RemoveDuplicates, QgsGeometryCheckError::StatusFixed,
  {{dup1LayerId, dup1Fid, QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId()}} ) );
  QgsFeature f;
  bool valid = testContext.second[dup1LayerId]->getFeature( dup1Fid, f );
  QVERIFY( !valid );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testDuplicateNodesCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryDuplicateNodesCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 4 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 0, QgsPointXY( -0.6360, 0.6203 ), QgsVertexId( 0, 0, 5 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 6, QgsPointXY( 0.2473, 2.0821 ), QgsVertexId( 0, 0, 1 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 6, QgsPointXY( 0.5158, 2.0930 ), QgsVertexId( 0, 0, 3 ) ).size() == 1 );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 4, QgsPointXY( 1.6319, 0.5642 ), QgsVertexId( 0, 0, 1 ) ) ).size() == 1 );

  // Test fixes
  QgsFeature f;

  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  int n1 = f.geometry().constGet()->vertexCount( errs1[0]->vidx().part, errs1[0]->vidx().ring );
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryDuplicateNodesCheck::RemoveDuplicates, QgsGeometryCheckError::StatusFixed,
  {{errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeRemoved, errs1[0]->vidx()}} ) );
  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  int n2 = f.geometry().constGet()->vertexCount( errs1[0]->vidx().part, errs1[0]->vidx().ring );
  QCOMPARE( n1, n2 + 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testFollowBoundariesCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "follow_ref.shp", "" );
  layers.insert( "follow_subj.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsFeedback feedback;
  QgsGeometryFollowBoundariesCheck( testContext.first, QVariantMap(), testContext.second[layers["follow_ref.shp"]]->layer() ).collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QCOMPARE( checkErrors.size(), 2 );
  QVERIFY( searchCheckErrors( checkErrors, layers["follow_subj.shp"], 1 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["follow_subj.shp"], 3 ).size() == 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testGapCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "gap_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "gapThreshold", 0.01 );

  QgsGeometryGapCheck check( testContext.first, configuration );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 5 );
  QVERIFY( searchCheckErrors( checkErrors, "", -1, QgsPointXY( 0.2924, -0.8798 ), QgsVertexId(), 0.0027 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, "", -1, QgsPointXY( 0.4238, -0.7479 ), QgsVertexId(), 0.0071 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, "", -1, QgsPointXY( 0.0094, -0.4448 ), QgsVertexId(), 0.0033 ).size() == 1 );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, "", -1, QgsPointXY( 0.2939, -0.4694 ), QgsVertexId(), 0.0053 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, "", -1, QgsPointXY( 0.6284, -0.3641 ), QgsVertexId(), 0.0018 ).size() == 1 );

  //  TestQgsGeometryChecks::testGapCheck()
  QgsGeometryCheckError *error = searchCheckErrors( checkErrors, "", -1, QgsPointXY( 0.2924, -0.8798 ), QgsVertexId(), 0.0027 ).first();

  QCOMPARE( error->contextBoundingBox().snappedToGrid( 0.0001 ), QgsRectangle( -0.0259, -1.0198, 0.6178, -0.4481 ) );
  QCOMPARE( error->affectedAreaBBox().snappedToGrid( 0.0001 ), QgsRectangle( 0.246, -0.9998, 0.3939, -0.77 ) );

  // Test fixes
  QgsFeature f;

  testContext.second[layers["gap_layer.shp"]]->getFeature( 0, f );
  double areaOld = f.geometry().area();
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryGapCheck::MergeLongestEdge, QgsGeometryCheckError::StatusFixed,
  {
    {layers["gap_layer.shp"], 0, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0 )},
    {layers["gap_layer.shp"], 0, QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 0 )}
  } ) );
  testContext.second[layers["gap_layer.shp"]]->getFeature( 0, f );
  QVERIFY( f.geometry().area() > areaOld );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testMissingVertexCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( QStringLiteral( "missing_vertex.gpkg" ), QString() );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryMissingVertexCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  const QString layerId = testContext.second.first()->layerId();
  QVERIFY( searchCheckErrors( checkErrors, layerId, 0, QgsPointXY( 0.251153, -0.460895 ), QgsVertexId() ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layerId, 3, QgsPointXY( 0.257985, -0.932886 ), QgsVertexId() ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layerId, 5, QgsPointXY( 0.59781, -0.480033 ), QgsVertexId() ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layerId, 5, QgsPointXY( 0.605252, -0.664875 ), QgsVertexId() ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layerId, 4, QgsPointXY( 0.259197, -0.478311 ), QgsVertexId() ).size() == 1 );

  QCOMPARE( checkErrors.size(), 5 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testHoleCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryHoleCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 12, QgsPointXY( 0.1902, 0.0951 ), QgsVertexId( 0, 1 ) ) ).size() == 1 );

  // Test fixes
  QgsFeature f;

  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryHoleCheck::RemoveHoles, QgsGeometryCheckError::StatusFixed,
  {
    {errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0, 1 )}
  } ) );
  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  QVERIFY( f.geometry().constGet()->ringCount( 0 ) == 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testLineIntersectionCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryLineIntersectionCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QCOMPARE( checkErrors.size(), 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 1, QgsPointXY( -0.5594, 0.4098 ), QgsVertexId( 0 ), QVariant( "line_layer.shp:0" ) ).size() == 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testLineLayerIntersectionCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "checkLayer", layers["polygon_layer.shp"] );

  QgsGeometryLineLayerIntersectionCheck check( testContext.first, configuration );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QCOMPARE( checkErrors.size(), 5 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 3, QgsPointXY( -0.1890, 0.9043 ), QgsVertexId( 0 ), QVariant( "polygon_layer.shp:3" ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 4, QgsPointXY( 0.9906, 1.1169 ), QgsVertexId( 0 ), QVariant( "polygon_layer.shp:2" ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 4, QgsPointXY( 1.0133, 1.0772 ), QgsVertexId( 0 ), QVariant( "polygon_layer.shp:2" ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 7, QgsPointXY( 0.9906, 1.1169 ), QgsVertexId( 0 ), QVariant( "polygon_layer.shp:2" ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 7, QgsPointXY( 1.0133, 1.0772 ), QgsVertexId( 0 ), QVariant( "polygon_layer.shp:2" ) ).size() == 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testMultipartCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryMultipartCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  // Easier to ensure that multipart features don't appear as errors than verifying each single-part multi-type feature
  QVERIFY( QgsWkbTypes::isSingleType( testContext.second[layers["point_layer.shp"]]->layer()->wkbType() ) );
  QVERIFY( QgsWkbTypes::isMultiType( testContext.second[layers["line_layer.shp"]]->layer()->wkbType() ) );
  QVERIFY( QgsWkbTypes::isMultiType( testContext.second[layers["polygon_layer.shp"]]->layer()->wkbType() ) );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).size() > 0 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"] ).size() > 0 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 0 ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 1 ).isEmpty() );

  // Two specific errors that will be fixed
  QList<QgsGeometryCheckError *> errs1;
  QList<QgsGeometryCheckError *> errs2;

  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["line_layer.shp"], 2, QgsPointXY( -0.6642, -0.2422 ) ) ).size() == 1 );
  QVERIFY( ( errs2 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 13, QgsPointXY( 0.5026, 3.0267 ) ) ).size() == 1 );

  // Test fixes
  QgsFeature f;
#if 0
  // The ogr provider apparently automatically re-converts the geometry type to a multitype...
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryMultipartCheck::ConvertToSingle, QgsGeometryCheckError::StatusFixed,
  {
    {errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeChanged, QgsVertexId( )}
  } ) );
  testContext.second[errs1[0]->layerId()]->get( errs1[0]->featureId(), f );
  QVERIFY( QgsWkbTypes::isSingleType( f.geometry().geometry()->wkbType() ) );
#endif

  QVERIFY( fixCheckError( testContext.second,  errs2[0],
                          QgsGeometryMultipartCheck::RemoveObject, QgsGeometryCheckError::StatusFixed,
  {
    {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeRemoved, QgsVertexId( )}
  } ) );
  bool valid = testContext.second[errs2[0]->layerId()]->getFeature( errs2[0]->featureId(), f );
  QVERIFY( !valid );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testOverlapCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );

  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "maxOverlapArea", 0.01 );

  QgsGeometryOverlapCheck check( testContext.first, configuration );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;

  QCOMPARE( checkErrors.size(), 2 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 10 ) ).size() == 2 );
  QVERIFY( messages.contains( "Overlap check failed for (polygon_layer.shp:1): the geometry is invalid" ) );

  // Test fixes
  QgsFeature f;

  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  double areaOld = f.geometry().area();
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometryOverlapCheck::Subtract, QgsGeometryCheckError::StatusFixed,
  {
    {errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeChanged, QgsVertexId( )}
  } ) );
  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  QVERIFY( f.geometry().area() < areaOld );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testOverlapCheckNoMaxArea()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( QStringLiteral( "point_layer.shp" ), QString() );
  layers.insert( QStringLiteral( "line_layer.shp" ), QString() );
  layers.insert( QStringLiteral( "polygon_layer.shp" ), QString() );

  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "maxOverlapArea", 0.0 );

  QgsGeometryOverlapCheck check( testContext.first, configuration );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;
  QCOMPARE( checkErrors.size(), 4 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 10 );
  QCOMPARE( errs1.size(), 2 );
  errs1 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 9 );
  QCOMPARE( errs1.size(), 2 );
}

void TestQgsGeometryChecks::testPointCoveredByLineCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryPointCoveredByLineCheck errs( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  errs.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"] ).isEmpty() );
  // Easier to test that no points which are covered by a line are marked as errors
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"], 0 ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"], 1 ).isEmpty() );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testPointInPolygonCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometryPointInPolygonCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"] ).isEmpty() );
  // Check that the point which is properly inside a polygon is not listed as error
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"], 5 ).isEmpty() );
  QVERIFY( messages.contains( "Point in polygon check failed for (polygon_layer.shp:1): the geometry is invalid" ) );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testSegmentLengthCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "minSegmentLength", 0.03 );

  QgsGeometrySegmentLengthCheck check( testContext.first, configuration );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QCOMPARE( checkErrors.size(), 4 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 3, QgsPointXY( 0.0753, 0.7921 ), QgsVertexId( 0, 0, 3 ), 0.0197 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 2, QgsPointXY( 0.7807, 1.1009 ), QgsVertexId( 0, 0, 0 ), 0.0176 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 10, QgsPointXY( -0.2819, 1.3553 ), QgsVertexId( 0, 0, 2 ), 0.0281 ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 11, QgsPointXY( -0.2819, 1.3553 ), QgsVertexId( 0, 0, 0 ), 0.0281 ).size() == 1 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testSelfContactCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometrySelfContactCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QCOMPARE( checkErrors.size(), 3 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 5, QgsPointXY( -1.2280, -0.8654 ), QgsVertexId( 0, 0, 0 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 5, QgsPointXY( -1.2399, -1.0502 ), QgsVertexId( 0, 0, 6 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 9, QgsPointXY( -0.2080, 1.9830 ), QgsVertexId( 0, 0, 3 ) ).size() == 1 );


  cleanupTestContext( testContext );

  // https://github.com/qgis/QGIS/issues/28228
  // test with a linestring which collapses to an empty linestring
  QgsGeometryCheckContext context( 1, QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3857" ) ), QgsCoordinateTransformContext() );
  QgsGeometrySelfContactCheck check2( &context, QVariantMap() );
  QgsGeometry g = QgsGeometry::fromWkt( QStringLiteral( "MultiLineString ((2988987 10262483, 2988983 10262480, 2988991 10262432, 2988990 10262419, 2988977 10262419, 2988976 10262420, 2988967 10262406, 2988970 10262421, 2988971 10262424),(2995620 10301368))" ) );
  QList<QgsSingleGeometryCheckError *> errors = check2.processGeometry( g );
  QVERIFY( errors.isEmpty() );

  // test with totally empty line
  qgsgeometry_cast< QgsMultiLineString * >( g.get() )->addGeometry( new QgsLineString() );
  errors = check2.processGeometry( g );
  QVERIFY( errors.isEmpty() );
}

void TestQgsGeometryChecks::testSelfIntersectionCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QgsGeometrySelfIntersectionCheck check( testContext.first, QVariantMap() );
  QgsFeedback feedback;
  check.collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QList<QgsGeometryCheckError *> errs1;
  QList<QgsGeometryCheckError *> errs2;
  QList<QgsGeometryCheckError *> errs3;
  QList<QgsGeometryCheckError *> errs4;

  QCOMPARE( checkErrors.size(), 5 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"], 1, QgsPointXY( -0.1199, 0.1440 ), QgsVertexId( 0, 0 ) ).size() == 1 );
  QVERIFY( ( errs1 = searchCheckErrors( checkErrors, layers["line_layer.shp"], 1, QgsPointXY( -0.1997, 0.1044 ), QgsVertexId( 0, 0 ) ) ).size() == 1 );
  QVERIFY( ( errs2 = searchCheckErrors( checkErrors, layers["line_layer.shp"], 8, QgsPointXY( -1.1985, 0.3128 ), QgsVertexId( 0, 0 ) ) ).size() == 1 );
  QVERIFY( ( errs3 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 1, QgsPointXY( 1.2592, 0.0888 ), QgsVertexId( 0, 0 ) ) ).size() == 1 );
  QVERIFY( ( errs4 = searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 12, QgsPointXY( 0.2213, 0.2365 ), QgsVertexId( 0, 0 ) ) ).size() == 1 );

  // Test fixes
  QgsFeature f;

  int nextId = testContext.second[errs1[0]->layerId()]->layer()->featureCount();
  QVERIFY( fixCheckError( testContext.second,  errs1[0],
                          QgsGeometrySelfIntersectionCheck::ToSingleObjects, QgsGeometryCheckError::StatusFixed,
  {
    {errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0 )},
    {errs1[0]->layerId(), errs1[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 0 )},
    {errs1[0]->layerId(), nextId, QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeAdded, QgsVertexId()}
  } ) );
  testContext.second[errs1[0]->layerId()]->getFeature( errs1[0]->featureId(), f );
  QCOMPARE( f.geometry().constGet()->partCount(), 1 );
  QCOMPARE( f.geometry().constGet()->vertexCount(), 4 );
  testContext.second[errs1[0]->layerId()]->getFeature( nextId, f );
  QCOMPARE( f.geometry().constGet()->partCount(), 1 );
  QCOMPARE( f.geometry().constGet()->vertexCount(), 6 );

  QVERIFY( fixCheckError( testContext.second,  errs2[0],
                          QgsGeometrySelfIntersectionCheck::ToMultiObject, QgsGeometryCheckError::StatusFixed,
  {
    {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0 )},
    {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 0 )},
    {errs2[0]->layerId(), errs2[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 1 )}
  } ) );
  testContext.second[errs2[0]->layerId()]->getFeature( errs2[0]->featureId(), f );
  QCOMPARE( f.geometry().constGet()->partCount(), 2 );
  QCOMPARE( f.geometry().constGet()->vertexCount( 0 ), 4 );
  QCOMPARE( f.geometry().constGet()->vertexCount( 1 ), 5 );

  nextId = testContext.second[errs3[0]->layerId()]->layer()->featureCount();
  QVERIFY( fixCheckError( testContext.second,  errs3[0],
                          QgsGeometrySelfIntersectionCheck::ToSingleObjects, QgsGeometryCheckError::StatusFixed,
  {
    {errs3[0]->layerId(), errs3[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeChanged, QgsVertexId( 0, 0 )},
    {errs3[0]->layerId(), nextId, QgsGeometryCheck::ChangeFeature, QgsGeometryCheck::ChangeAdded, QgsVertexId()}
  } ) );
  testContext.second[errs3[0]->layerId()]->getFeature( errs3[0]->featureId(), f );
  QCOMPARE( f.geometry().constGet()->partCount(), 1 );
  QCOMPARE( f.geometry().constGet()->vertexCount(), 6 );
  testContext.second[errs3[0]->layerId()]->getFeature( nextId, f );
  QCOMPARE( f.geometry().constGet()->partCount(), 1 );
  QCOMPARE( f.geometry().constGet()->vertexCount(), 4 );

  QVERIFY( fixCheckError( testContext.second,  errs4[0],
                          QgsGeometrySelfIntersectionCheck::ToMultiObject, QgsGeometryCheckError::StatusFixed,
  {
    {errs4[0]->layerId(), errs4[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeChanged, QgsVertexId( 0, 0 )},
    {errs4[0]->layerId(), errs4[0]->featureId(), QgsGeometryCheck::ChangeRing, QgsGeometryCheck::ChangeRemoved, QgsVertexId( 0, 1 )},
    {errs4[0]->layerId(), errs4[0]->featureId(), QgsGeometryCheck::ChangePart, QgsGeometryCheck::ChangeAdded, QgsVertexId( 1 )}
  } ) );
  testContext.second[errs4[0]->layerId()]->getFeature( errs4[0]->featureId(), f );
  QCOMPARE( f.geometry().constGet()->partCount(), 2 );
  QCOMPARE( f.geometry().constGet()->ringCount( 0 ), 1 );
  QCOMPARE( f.geometry().constGet()->vertexCount( 0, 0 ), 5 );
  QCOMPARE( f.geometry().constGet()->ringCount( 1 ), 2 );
  QCOMPARE( f.geometry().constGet()->vertexCount( 1, 0 ), 5 );
  QCOMPARE( f.geometry().constGet()->vertexCount( 1, 1 ), 5 );

  // Test change tracking
  QgsGeometryCheckErrorSingle *err = static_cast<QgsGeometryCheckErrorSingle *>( errs4[0] );
  QgsGeometryUtils::SelfIntersection oldInter = static_cast<QgsGeometrySelfIntersectionCheckError *>( err->singleError() )->intersection();
  QVERIFY( err->handleChanges( change2changes( {err->layerId(), err->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeRemoved, QgsVertexId( err->vidx().part, errs4[0]->vidx().ring, 0 )} ) ) );
  QgsGeometryUtils::SelfIntersection  newInter = static_cast<QgsGeometrySelfIntersectionCheckError *>( err->singleError() )->intersection();
  QVERIFY( oldInter.segment1 == newInter.segment1 + 1 );
  QVERIFY( oldInter.segment2 == newInter.segment2 + 1 );
  QVERIFY( err->handleChanges( change2changes( {err->layerId(), errs4[0]->featureId(), QgsGeometryCheck::ChangeNode, QgsGeometryCheck::ChangeAdded, QgsVertexId( err->vidx().part, errs4[0]->vidx().ring, 0 )} ) ) );
  newInter = static_cast<QgsGeometrySelfIntersectionCheckError *>( err->singleError() )->intersection();
  QVERIFY( oldInter.segment1 == newInter.segment1 );
  QVERIFY( oldInter.segment2 == newInter.segment2 );

  cleanupTestContext( testContext );
}

void TestQgsGeometryChecks::testSliverPolygonCheck()
{
  QTemporaryDir dir;
  QMap<QString, QString> layers;
  layers.insert( "point_layer.shp", "" );
  layers.insert( "line_layer.shp", "" );
  layers.insert( "polygon_layer.shp", "" );
  auto testContext = createTestContext( dir, layers );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configuration;
  configuration.insert( "threshold", 20 );
  configuration.insert( "maxArea", 0.04 );

  QgsFeedback feedback;
  QgsGeometrySliverPolygonCheck( testContext.first, configuration ).collectErrors( testContext.second, checkErrors, messages, &feedback );
  listErrors( checkErrors, messages );

  QCOMPARE( checkErrors.size(), 2 );
  QVERIFY( searchCheckErrors( checkErrors, layers["point_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["line_layer.shp"] ).isEmpty() );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 6, QgsPointXY(), QgsVertexId( 0 ) ).size() == 1 );
  QVERIFY( searchCheckErrors( checkErrors, layers["polygon_layer.shp"], 11, QgsPointXY(), QgsVertexId( 0 ) ).size() == 1 );

  // The fix methods are exactely the same as in QgsGeometryAreaCheck, no point repeating...

  cleanupTestContext( testContext );
}

///////////////////////////////////////////////////////////////////////////////

double TestQgsGeometryChecks::layerToMapUnits( const QgsMapLayer *layer, const QgsCoordinateReferenceSystem &mapCrs ) const
{
  QgsCoordinateTransform crst = QgsCoordinateTransform( layer->crs(), mapCrs, QgsProject::instance() );
  QgsRectangle extent = layer->extent();
  QgsPointXY l1( extent.xMinimum(), extent.yMinimum() );
  QgsPointXY l2( extent.xMaximum(), extent.yMaximum() );
  double distLayerUnits = std::sqrt( l1.sqrDist( l2 ) );
  QgsPointXY m1 = crst.transform( l1 );
  QgsPointXY m2 = crst.transform( l2 );
  double distMapUnits = std::sqrt( m1.sqrDist( m2 ) );
  return distMapUnits / distLayerUnits;
}

QgsFeaturePool *TestQgsGeometryChecks::createFeaturePool( QgsVectorLayer *layer, bool selectedOnly ) const
{
  return new QgsVectorDataProviderFeaturePool( layer, selectedOnly );
}

QPair<QgsGeometryCheckContext *, QMap<QString, QgsFeaturePool *> > TestQgsGeometryChecks::createTestContext( QTemporaryDir &tempDir, QMap<QString, QString> &layers, const QgsCoordinateReferenceSystem &mapCrs, double prec ) const
{
  QDir testDataDir( QDir( TEST_DATA_DIR ).absoluteFilePath( "geometry_checker" ) );
  QDir tmpDir( tempDir.path() );

  QMap<QString, QgsFeaturePool *> featurePools;
  for ( const QString &layerFile : layers.keys() )
  {
    QFile( testDataDir.absoluteFilePath( layerFile ) ).copy( tmpDir.absoluteFilePath( layerFile ) );
    if ( layerFile.endsWith( ".shp", Qt::CaseInsensitive ) )
    {
      QString baseName = QFileInfo( layerFile ).baseName();
      QFile( testDataDir.absoluteFilePath( baseName + ".dbf" ) ).copy( tmpDir.absoluteFilePath( baseName + ".dbf" ) );
      QFile( testDataDir.absoluteFilePath( baseName + ".pri" ) ).copy( tmpDir.absoluteFilePath( baseName + ".pri" ) );
      QFile( testDataDir.absoluteFilePath( baseName + ".qpj" ) ).copy( tmpDir.absoluteFilePath( baseName + ".qpj" ) );
      QFile( testDataDir.absoluteFilePath( baseName + ".shx" ) ).copy( tmpDir.absoluteFilePath( baseName + ".shx" ) );
    }
    QgsVectorLayer *layer = new QgsVectorLayer( tmpDir.absoluteFilePath( layerFile ), layerFile );
    Q_ASSERT( layer && layer->isValid() );
    layers[layerFile] = layer->id();
    layer->dataProvider()->enterUpdateMode();
    featurePools.insert( layer->id(), createFeaturePool( layer ) );
  }
  return qMakePair( new QgsGeometryCheckContext( prec, mapCrs, QgsProject::instance()->transformContext() ), featurePools );
}

void TestQgsGeometryChecks::cleanupTestContext( QPair<QgsGeometryCheckContext *, QMap<QString, QgsFeaturePool *> > ctx ) const
{
  for ( const QgsFeaturePool *pool : ctx.second )
  {
    pool->layer()->dataProvider()->leaveUpdateMode();
    delete pool->layer();
  }
  qDeleteAll( ctx.second );
  delete ctx.first;
}

void TestQgsGeometryChecks::listErrors( const QList<QgsGeometryCheckError *> &checkErrors, const QStringList &messages ) const
{
  QTextStream( stdout ) << " - Check result:" << endl;
  for ( const QgsGeometryCheckError *error : checkErrors )
  {
    QTextStream( stdout ) << "   * " << error->layerId() << ":" << error->featureId() << " @[" << error->vidx().part << ", " << error->vidx().ring << ", " << error->vidx().vertex << "](" << error->location().x() << ", " << error->location().y() << ") = " << error->value().toString() << endl;
  }
  if ( !messages.isEmpty() )
  {
    QTextStream( stdout ) << " - Check messages:" << endl << "   * " << messages.join( "\n   * " ) << endl;
  }
}

QList<QgsGeometryCheckError *> TestQgsGeometryChecks::searchCheckErrors( const QList<QgsGeometryCheckError *> &checkErrors, const QString &layerId, const QgsFeatureId &featureId, const QgsPointXY &pos, const QgsVertexId &vid, const QVariant &value, double tol ) const
{
  QList<QgsGeometryCheckError *> matching;
  for ( QgsGeometryCheckError *error : checkErrors )
  {
    if ( error->layerId() != layerId )
    {
      continue;
    }
    if ( featureId != -1 && error->featureId() != featureId )
    {
      continue;
    }
    if ( !pos.isEmpty() && ( !qgsDoubleNear( error->location().x(), pos.x(), tol ) || !qgsDoubleNear( error->location().y(), pos.y(), tol ) ) )
    {
      continue;
    }
    if ( vid.isValid() && vid != error->vidx() )
    {
      continue;
    }
    if ( !value.isNull() )
    {
      if ( value.type() == QVariant::Double )
      {
        if ( !qgsDoubleNear( value.toDouble(), error->value().toDouble(), tol ) )
        {
          continue;
        }
      }
      else if ( value != error->value() )
      {
        continue;
      }
    }
    matching.append( error );
  }
  return matching;
}

bool TestQgsGeometryChecks::fixCheckError( QMap<QString, QgsFeaturePool *> featurePools, QgsGeometryCheckError *error, int method, const QgsGeometryCheckError::Status &expectedStatus, const QVector<Change> &expectedChanges, const QMap<QString, int> &mergeAttrs )
{
  QTextStream( stdout ) << " - Fixing " << error->layerId() << ":" << error->featureId() << " @[" << error->vidx().part << ", " << error->vidx().ring << ", " << error->vidx().vertex << "](" << error->location().x() << ", " << error->location().y() << ") = " << error->value().toString() << endl;
  QgsGeometryCheck::Changes changes;
  error->check()->fixError( featurePools, error, method, mergeAttrs, changes );
  QTextStream( stdout ) << "   * Fix status: " << error->status() << endl;
  if ( error->status() != expectedStatus )
  {
    return false;
  }
  QString strChangeWhat[] = { "ChangeFeature", "ChangePart", "ChangeRing", "ChangeNode" };
  QString strChangeType[] = { "ChangeAdded", "ChangeRemoved", "ChangeChanged" };
  int totChanges = 0;
  for ( const QString &layerId : changes.keys() )
  {
    for ( const QgsFeatureId &fid : changes[layerId].keys() )
    {
      for ( const QgsGeometryCheck::Change &change : changes[layerId][fid] )
      {
        QTextStream( stdout ) << "   * Change: " << layerId << ":" << fid << " :: " << strChangeWhat[change.what] << ", " << strChangeType[change.type] << ", " << change.vidx.part << ":" << change.vidx.ring << ":" << change.vidx.vertex << endl;
      }
      totChanges += changes[layerId][fid].size();
    }
  }
  QTextStream( stdout ) << "   * Num changes: " << totChanges << ", expected num changes: " << expectedChanges.size() << endl;
  if ( expectedChanges.size() != totChanges )
  {
    return false;
  }
  for ( const Change &expectedChange : expectedChanges )
  {
    if ( !changes.contains( expectedChange.layerId ) )
    {
      return false;
    }
    if ( !changes[expectedChange.layerId].contains( expectedChange.fid ) )
    {
      return false;
    }
    QgsGeometryCheck::Change change( expectedChange.what, expectedChange.type, expectedChange.vidx );
    if ( !changes[expectedChange.layerId][expectedChange.fid].contains( change ) )
    {
      return false;
    }
  }
  return true;
}

QgsGeometryCheck::Changes TestQgsGeometryChecks::change2changes( const Change &change ) const
{
  QgsGeometryCheck::Changes changes;
  QMap<QgsFeatureId, QList<QgsGeometryCheck::Change>> featureChanges;
  featureChanges.insert( change.fid, {QgsGeometryCheck::Change( change.what, change.type, change.vidx )} );
  changes.insert( change.layerId, featureChanges );
  return changes;
}

QGSTEST_MAIN( TestQgsGeometryChecks )
#include "testqgsgeometrychecks.moc"
