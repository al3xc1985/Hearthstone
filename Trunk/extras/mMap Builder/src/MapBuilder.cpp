/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "MMapCommon.h"
#include "MapBuilder.h"

#include "MapTree.h"
#include "ModelInstance.h"

#include "DetourNavMeshBuilder.h"
#include "DetourCommon.h"

using namespace VMAP;

namespace MMAP
{
    MapBuilder::MapBuilder(float maxWalkableAngle, bool skipLiquid,
                           bool skipContinents, bool skipJunkMaps, bool skipBattlegrounds,
                           bool debugOutput, bool bigBaseUnit, const char* offMeshFilePath) :
                           m_terrainBuilder(NULL),
                           m_debugOutput        (debugOutput),
                           m_skipContinents     (skipContinents),
                           m_skipJunkMaps       (skipJunkMaps),
                           m_skipBattlegrounds  (skipBattlegrounds),
                           m_maxWalkableAngle   (maxWalkableAngle),
                           m_bigBaseUnit        (bigBaseUnit),
                           m_rcContext          (NULL),
                           m_offMeshFilePath    (offMeshFilePath)
    {
        m_terrainBuilder = new TerrainBuilder(skipLiquid);

        m_rcContext = new rcContext(false);

        discoverTiles();
    }

    /**************************************************************************/
    MapBuilder::~MapBuilder()
    {
        for (TileList::iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
        {
            (*it).second->clear();
            delete (*it).second;
        }

        delete m_terrainBuilder;
        delete m_rcContext;
    }

    /**************************************************************************/
    void MapBuilder::discoverTiles()
    {
        vector<string> files;
        uint32 mapID, tileX, tileY, tileID, count = 0;
        char filter[12];

        printf("Discovering maps... ");
        getDirContents(files, "maps");
        for (uint32 i = 0; i < files.size(); ++i)
        {
            mapID = uint32(atoi(files[i].substr(0,3).c_str()));
            if (m_tiles.find(mapID) == m_tiles.end())
            {
                m_tiles.insert(pair<uint32,set<uint32>*>(mapID, new set<uint32>));
                count++;
            }
        }

        files.clear();
        getDirContents(files, "vmaps", "*.vmtree");
        for (uint32 i = 0; i < files.size(); ++i)
        {
            mapID = uint32(atoi(files[i].substr(0,3).c_str()));
            m_tiles.insert(pair<uint32,set<uint32>*>(mapID, new set<uint32>));
            count++;
        }
        printf("found %u.\n", count);

        count = 0;
        printf("Discovering tiles... ");
        for (TileList::iterator itr = m_tiles.begin(); itr != m_tiles.end(); ++itr)
        {
            set<uint32>* tiles = (*itr).second;
            mapID = (*itr).first;

            sprintf(filter, "%03u*.vmtile", mapID);
            files.clear();
            getDirContents(files, "vmaps", filter);
            for (uint32 i = 0; i < files.size(); ++i)
            {
                tileX = uint32(atoi(files[i].substr(7,2).c_str()));
                tileY = uint32(atoi(files[i].substr(4,2).c_str()));
                tileID = StaticMapTree::packTileID(tileY, tileX);

                tiles->insert(tileID);
                count++;
            }

            sprintf(filter, "%03u*", mapID);
            files.clear();
            getDirContents(files, "maps", filter);
            for (uint32 i = 0; i < files.size(); ++i)
            {
                tileY = uint32(atoi(files[i].substr(3,2).c_str()));
                tileX = uint32(atoi(files[i].substr(5,2).c_str()));
                tileID = StaticMapTree::packTileID(tileX, tileY);

                if (tiles->insert(tileID).second)
                    count++;
            }
        }
        printf("found %u.\n\n", count);
    }

    /**************************************************************************/
    set<uint32>* MapBuilder::getTileList(uint32 mapID)
    {
        TileList::iterator itr = m_tiles.find(mapID);
        if (itr != m_tiles.end())
            return (*itr).second;

        set<uint32>* tiles = new set<uint32>();
        m_tiles.insert(pair<uint32, set<uint32>*>(mapID, tiles));
        return tiles;
    }

    /**************************************************************************/
    void MapBuilder::buildAllMaps()
    {
        for (TileList::iterator it = m_tiles.begin(); it != m_tiles.end(); ++it)
        {
            uint32 mapID = (*it).first;
            if (!shouldSkipMap(mapID))
                buildMap(mapID);
        }
    }

    /**************************************************************************/
    void MapBuilder::getGridBounds(uint32 mapID, uint32 &minX, uint32 &minY, uint32 &maxX, uint32 &maxY)
    {
        maxX = INT_MAX;
        maxY = INT_MAX;
        minX = INT_MIN;
        minY = INT_MIN;

        float bmin[3], bmax[3], lmin[3], lmax[3];
        MeshData meshData;

        // make sure we process maps which don't have tiles
        // initialize the static tree, which loads WDT models
        if(!m_terrainBuilder->loadVMap(mapID, 64, 64, meshData))
            return;

        // get the coord bounds of the model data
        if (meshData.solidVerts.size() + meshData.liquidVerts.size() == 0)
            return;

        // get the coord bounds of the model data
        if (meshData.solidVerts.size() && meshData.liquidVerts.size())
        {
            rcCalcBounds(meshData.solidVerts.getCArray(), meshData.solidVerts.size() / 3, bmin, bmax);
            rcCalcBounds(meshData.liquidVerts.getCArray(), meshData.liquidVerts.size() / 3, lmin, lmax);
            rcVmin(bmin, lmin);
            rcVmax(bmax, lmax);
        }
        else if (meshData.solidVerts.size())
            rcCalcBounds(meshData.solidVerts.getCArray(), meshData.solidVerts.size() / 3, bmin, bmax);
        else
            rcCalcBounds(meshData.liquidVerts.getCArray(), meshData.liquidVerts.size() / 3, lmin, lmax);

        // convert coord bounds to grid bounds
        maxX = 32 - bmin[0] / GRID_SIZE;
        maxY = 32 - bmin[2] / GRID_SIZE;
        minX = 32 - bmax[0] / GRID_SIZE;
        minY = 32 - bmax[2] / GRID_SIZE;
    }

    /**************************************************************************/
    void MapBuilder::buildSingleTile(uint32 mapID, uint32 tileX, uint32 tileY)
    {
        dtNavMesh* navMesh = NULL;
        buildNavMesh(mapID, navMesh);
        if (!navMesh)
        {
            printf("Failed creating navmesh!              \n");
            return;
        }

        buildTile(mapID, tileX, tileY, navMesh, 0);
        freeNavMesh(navMesh);
    }

    /**************************************************************************/
    void MapBuilder::buildMap(uint32 mapID)
    {
        printf("Building map %03u:\n", mapID);

        set<uint32>* tiles = getTileList(mapID);

		// make sure we process maps which don't have tiles
        if (!tiles->size())
        {
            // convert coord bounds to grid bounds
            uint32 minX, minY, maxX, maxY;
            getGridBounds(mapID, minX, minY, maxX, maxY);

            // add all tiles within bounds to tile list.
            for (uint32 i = minX; i <= maxX; ++i)
                for (uint32 j = minY; j <= maxY; ++j)
                    tiles->insert(StaticMapTree::packTileID(i, j));
        }

        if (!tiles->size())
            return;

        // build navMesh
        dtNavMesh* navMesh = NULL;
        buildNavMesh(mapID, navMesh);
        if (!navMesh)
        {
            printf("Failed creating navmesh!              \n");
            return;
        }

		uint32 TileCount = 0;
        // now start building mmtiles for each tile
        printf("We have %u tiles.                          \n", (unsigned int)tiles->size());
        for (set<uint32>::iterator it = tiles->begin(); it != tiles->end(); ++it)
        {
            uint32 tileX, tileY;

            // unpack tile coords
            StaticMapTree::unpackTileID((*it), tileX, tileY);

            if (shouldSkipTile(mapID, tileX, tileY))
                continue;

            buildTile(mapID, tileX, tileY, navMesh, TileCount++);
        }

        freeNavMesh(navMesh);

        printf("Complete!                               \n\n");
    }

    /**************************************************************************/
    void MapBuilder::buildTile(uint32 mapID, uint32 tileX, uint32 tileY, dtNavMesh* navMesh, uint32 index)
    {
        printf("Building map %03u[%02u,%02u] #:%04u     \r", mapID, tileX, tileY, index);

        MeshData meshData;

        // get heightmap data
        m_terrainBuilder->loadMap(mapID, tileX, tileY, meshData);

        // get model data
        m_terrainBuilder->loadVMap(mapID, tileY, tileX, meshData);

        // if there is no data, give up now
        if (!meshData.solidVerts.size() && !meshData.liquidVerts.size())
            return;

        // remove unused vertices
        TerrainBuilder::cleanVertices(meshData.solidVerts, meshData.solidTris);
        TerrainBuilder::cleanVertices(meshData.liquidVerts, meshData.liquidTris);

        // gather all mesh data for final data check, and bounds calculation
        G3D::Array<float> allVerts;
        allVerts.append(meshData.liquidVerts);
        allVerts.append(meshData.solidVerts);

        if (!allVerts.size())
            return;

        // get bounds of current tile
        float bmin[3], bmax[3];
        getTileBounds(tileX, tileY, allVerts.getCArray(), allVerts.size() / 3, bmin, bmax);

        m_terrainBuilder->loadOffMeshConnections(mapID, tileX, tileY, meshData, m_offMeshFilePath);

        // build navmesh tile
        buildMoveMapTile(mapID, tileX, tileY, meshData, bmin, bmax, navMesh, index);
    }

    /**************************************************************************/
    void MapBuilder::buildNavMesh(uint32 mapID, dtNavMesh* &navMesh)
    {
        set<uint32>* tiles = getTileList(mapID);

        // old code for non-statically assigned bitmask sizes:
        ///*** calculate number of bits needed to store tiles & polys ***/
        //int tileBits = dtIlog2(dtNextPow2(tiles->size()));
        //if (tileBits < 1) tileBits = 1;                                     // need at least one bit!
        //int polyBits = sizeof(dtPolyRef)*8 - SALT_MIN_BITS - tileBits;

        int tileBits = STATIC_TILE_BITS;
        int polyBits = STATIC_POLY_BITS;

        int maxTiles = tiles->size();
        int maxPolysPerTile = 1 << polyBits;

        /***          calculate bounds of map         ***/
        
        uint32 tileXMin = 64, tileYMin = 64, tileXMax = 0, tileYMax = 0, tileX, tileY;
        for (set<uint32>::iterator it = tiles->begin(); it != tiles->end(); ++it)
        {
            StaticMapTree::unpackTileID((*it), tileX, tileY);

            if (tileX > tileXMax)
                tileXMax = tileX;
            else if (tileX < tileXMin)
                tileXMin = tileX;

            if (tileY > tileYMax)
                tileYMax = tileY;
            else if (tileY < tileYMin)
                tileYMin = tileY;
        }

        // use Max because '32 - tileX' is negative for values over 32
        float bmin[3], bmax[3];
        getTileBounds(tileXMax, tileYMax, NULL, 0, bmin, bmax);

        /***       now create the navmesh       ***/

        // navmesh creation params
        dtNavMeshParams navMeshParams;
        memset(&navMeshParams, 0, sizeof(dtNavMeshParams));
        navMeshParams.tileWidth = GRID_SIZE;
        navMeshParams.tileHeight = GRID_SIZE;
        rcVcopy(navMeshParams.orig, bmin);
        navMeshParams.maxTiles = maxTiles;
        navMeshParams.maxPolys = maxPolysPerTile;

        navMesh = mallocNavMesh();
        printf("Creating navMesh...                     \r");
        if (!navMesh->init(&navMeshParams))
        {
            printf("Failed creating navmesh!                \n");
            return;
        }

        char fileName[25];
        sprintf(fileName, "mmaps/%03u.mmap", mapID);

        FILE* file = fopen(fileName, "wb");
        if (!file)
        {
            freeNavMesh(navMesh);
            char message[1024];
            sprintf(message, "Failed to open %s for writing!\n", fileName);
            perror(message);
            return;
        }

        // now that we know navMesh params are valid, we can write them to file
        fwrite(&navMeshParams, sizeof(dtNavMeshParams), 1, file);
        fclose(file);
    }

    /**************************************************************************/
    void MapBuilder::buildMoveMapTile(uint32 mapID, uint32 tileX, uint32 tileY,
                                      MeshData &meshData, float bmin[3], float bmax[3],
                                      dtNavMesh* navMesh, uint32 index)
    {
        // console output
        char tileString[10];
        sprintf(tileString, "[%02i,%02i]: ", tileX, tileY);
        printf("%s Building movemap tiles... #:%04u                 \r", tileString, index);

        IntermediateValues iv;

        float* tVerts = meshData.solidVerts.getCArray();
        int tVertCount = meshData.solidVerts.size() / 3;
        int* tTris = meshData.solidTris.getCArray();
        int tTriCount = meshData.solidTris.size() / 3;

        float* lVerts = meshData.liquidVerts.getCArray();
        int lVertCount = meshData.liquidVerts.size() / 3;
        int* lTris = meshData.liquidTris.getCArray();
        int lTriCount = meshData.liquidTris.size() / 3;
        uint8* lTriFlags = meshData.liquidType.getCArray();

        // these are WORLD UNIT based metrics
        // this are basic unit dimentions
        // value have to divide GRID_SIZE(533.33333f) ( aka: 0.5333, 0.2666, 0.3333, 0.1333, etc )
        const static float BASE_UNIT_DIM = m_bigBaseUnit ? 0.533333f : 0.266666f;

        // All are in UNIT metrics!
        const static int VERTEX_PER_MAP = int(GRID_SIZE/BASE_UNIT_DIM + 0.5f);
        const static int VERTEX_PER_TILE = m_bigBaseUnit ? 40 : 80; // must divide VERTEX_PER_MAP
        const static int TILES_PER_MAP = VERTEX_PER_MAP/VERTEX_PER_TILE;

        rcConfig config;
        memset(&config, 0, sizeof(rcConfig));

        rcVcopy(config.bmin, bmin);
        rcVcopy(config.bmax, bmax);

        config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
        config.cs = BASE_UNIT_DIM;
        config.ch = BASE_UNIT_DIM;
        config.walkableSlopeAngle = m_maxWalkableAngle;
        config.tileSize = VERTEX_PER_TILE;
        config.walkableRadius = m_bigBaseUnit ? 1 : 2;
        config.borderSize = config.walkableRadius + 3;
        config.maxEdgeLen = VERTEX_PER_TILE + 1;        //anything bigger than tileSize
        config.walkableHeight = m_bigBaseUnit ? 3 : 6;
        config.walkableClimb = m_bigBaseUnit ? 2 : 4;   // keep less than walkableHeight
        config.minRegionArea = rcSqr(60);
        config.mergeRegionArea = rcSqr(50);
        config.maxSimplificationError = 2.0f;       // eliminates most jagged edges (tinny polygons)
        config.detailSampleDist = config.cs * 64;
        config.detailSampleMaxError = config.ch * 2;

        // this sets the dimensions of the heightfield - should maybe happen before border padding
        rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);

        // allocate subregions : tiles
        Tile* tiles = new Tile[TILES_PER_MAP * TILES_PER_MAP];

        // Initialize per tile config.
        rcConfig tileCfg;
        memcpy(&tileCfg, &config, sizeof(rcConfig));
        tileCfg.width = config.tileSize + config.borderSize*2;
        tileCfg.height = config.tileSize + config.borderSize*2;

        // build all tiles
        for (int y = 0; y < TILES_PER_MAP; ++y)
        {
            for (int x = 0; x < TILES_PER_MAP; ++x)
            {
                Tile& tile = tiles[x + y*TILES_PER_MAP];

                // Calculate the per tile bounding box.
                tileCfg.bmin[0] = config.bmin[0] + (x*config.tileSize - config.borderSize)*config.cs;
                tileCfg.bmin[2] = config.bmin[2] + (y*config.tileSize - config.borderSize)*config.cs;
                tileCfg.bmax[0] = config.bmin[0] + ((x+1)*config.tileSize + config.borderSize)*config.cs;
                tileCfg.bmax[2] = config.bmin[2] + ((y+1)*config.tileSize + config.borderSize)*config.cs;

                float tbmin[2], tbmax[2];
                tbmin[0] = tileCfg.bmin[0];
                tbmin[1] = tileCfg.bmin[2];
                tbmax[0] = tileCfg.bmax[0];
                tbmax[1] = tileCfg.bmax[2];

                // build heightfield
                tile.solid = mallocHeightfield();
                if (!tile.solid || !rcCreateHeightfield(m_rcContext, *tile.solid, tileCfg.width, tileCfg.height, tileCfg.bmin, tileCfg.bmax, tileCfg.cs, tileCfg.ch))
                {
                    printf("%sFailed building heightfield! #:%04u     \n", tileString, index);
                    continue;
                }

                // mark all walkable tiles, both liquids and solids
                unsigned char* triFlags = new unsigned char[tTriCount];
                memset(triFlags, NAV_GROUND, tTriCount*sizeof(unsigned char));
                rcClearUnwalkableTriangles(m_rcContext, tileCfg.walkableSlopeAngle, tVerts, tVertCount, tTris, tTriCount, triFlags);
                rcRasterizeTriangles(m_rcContext, tVerts, tVertCount, tTris, triFlags, tTriCount, *tile.solid, config.walkableClimb);
                delete [] triFlags;

                rcFilterLowHangingWalkableObstacles(m_rcContext, config.walkableClimb, *tile.solid);
                rcFilterLedgeSpans(m_rcContext, tileCfg.walkableHeight, tileCfg.walkableClimb, *tile.solid);
                rcFilterWalkableLowHeightSpans(m_rcContext, tileCfg.walkableHeight, *tile.solid);

                rcRasterizeTriangles(m_rcContext, lVerts, lVertCount, lTris, lTriFlags, lTriCount, *tile.solid, config.walkableClimb);

                // compact heightfield spans
                tile.chf = mallocCompactHeightfield();
                if (!tile.chf || !rcBuildCompactHeightfield(m_rcContext, tileCfg.walkableHeight, tileCfg.walkableClimb, *tile.solid, *tile.chf))
                {
                    printf("%sFailed compacting heightfield! #:%04u     \n", tileString, index);
                    continue;
                }

                // build polymesh intermediates
                if (!rcErodeWalkableArea(m_rcContext, config.walkableRadius, *tile.chf))
                {
                    printf("%sFailed eroding area! #:%04u             \n", tileString, index);
                    continue;
                }

                if (!rcBuildDistanceField(m_rcContext, *tile.chf))
                {
                    printf("%sFailed building distance field! #:%04u  \n", tileString, index);
                    continue;
                }

                if (!rcBuildRegions(m_rcContext, *tile.chf, tileCfg.borderSize, tileCfg.minRegionArea, tileCfg.mergeRegionArea))
                {
                    printf("%sFailed building regions! #:%04u         \n", tileString, index);
                    continue;
                }

                tile.cset = mallocContourSet();
                if (!tile.cset || !rcBuildContours(m_rcContext, *tile.chf, tileCfg.maxSimplificationError, tileCfg.maxEdgeLen, *tile.cset))
                {
                    printf("%sFailed building contours! #:%04u        \n", tileString, index);
                    continue;
                }

                // build polymesh
                tile.pmesh = mallocPolyMesh();
                if (!tile.pmesh || !rcBuildPolyMesh(m_rcContext, *tile.cset, tileCfg.maxVertsPerPoly, *tile.pmesh))
                {
                    printf("%sFailed building polymesh! #:%04u        \n", tileString, index);
                    continue;
                }

                tile.dmesh = mallocPolyMeshDetail();
                if (!tile.dmesh || !rcBuildPolyMeshDetail(m_rcContext, *tile.pmesh, *tile.chf, tileCfg.detailSampleDist, tileCfg    .detailSampleMaxError, *tile.dmesh))
                {
					printf("%sFailed building polymesh detail! #:%04u \n", tileString, index);
                    continue;
                }

                // free those up
                // we may want to keep them in the future for debug
                // but right now, we don't have the code to merge them
                freeHeightField(tile.solid);
                tile.solid = NULL;
                freeCompactHeightfield(tile.chf);
                tile.chf = NULL;
                freeContourSet(tile.cset);
                tile.cset = NULL;
            }
        }

        // merge per tile poly and detail meshes
        rcPolyMesh** pmmerge = new rcPolyMesh*[TILES_PER_MAP * TILES_PER_MAP];
        if (!pmmerge)
        {
            printf("%s alloc pmmerge FAILED! #:%04u   \r", tileString, index);
            return;
        }

        rcPolyMeshDetail** dmmerge = new rcPolyMeshDetail*[TILES_PER_MAP * TILES_PER_MAP];
        if (!dmmerge)
        {
            printf("%s alloc dmmerge FAILED! #:%04u   \r", tileString, index);
            return;
        }

        int nmerge = 0;
        for (int y = 0; y < TILES_PER_MAP; ++y)
        {
            for (int x = 0; x < TILES_PER_MAP; ++x)
            {
                Tile& tile = tiles[x + y*TILES_PER_MAP];
                if (tile.pmesh)
                {
                    pmmerge[nmerge] = tile.pmesh;
                    dmmerge[nmerge] = tile.dmesh;
                    nmerge++;
                }
            }
        }

        iv.polyMesh = mallocPolyMesh();
        if (!iv.polyMesh)
        {
            printf("%s alloc iv.polyMesh FAILED! #:%04u   \r", tileString, index);
            return;
        }
        rcMergePolyMeshes(m_rcContext, pmmerge, nmerge, *iv.polyMesh);

        iv.polyMeshDetail = mallocPolyMeshDetail();
        if (!iv.polyMeshDetail)
        {
            printf("%s alloc m_dmesh FAILED! #:%04u   \r", tileString, index);
            return;
        }
        rcMergePolyMeshDetails(m_rcContext, dmmerge, nmerge, *iv.polyMeshDetail);

        // free things up
        delete [] pmmerge;
        delete [] dmmerge;

        delete [] tiles;

        // remove padding for extraction
        for (int i = 0; i < iv.polyMesh->nverts; ++i)
        {
            unsigned short* v = &iv.polyMesh->verts[i*3];
            v[0] -= (unsigned short)config.borderSize;
            v[2] -= (unsigned short)config.borderSize;
        }

        // set polygons as walkable
        // TODO: special flags for DYNAMIC polygons, ie surfaces that can be turned on and off
        for (int i = 0; i < iv.polyMesh->npolys; ++i)
            if (iv.polyMesh->areas[i] & RC_WALKABLE_AREA)
                iv.polyMesh->flags[i] = iv.polyMesh->areas[i];

        // setup mesh parameters
        dtNavMeshCreateParams params;
        memset(&params, 0, sizeof(params));

		/// Initialize our vertices
        params.verts = iv.polyMesh->verts;
        params.vertCount = iv.polyMesh->nverts;

		/// Initialize our Polygons
        params.polys = iv.polyMesh->polys;
        params.polyAreas = iv.polyMesh->areas;
        params.polyFlags = iv.polyMesh->flags;
        params.polyCount = iv.polyMesh->npolys;
        params.nvp = iv.polyMesh->nvp;

		/// Initialize our detail :D
        params.detailMeshes = iv.polyMeshDetail->meshes;
        params.detailVerts = iv.polyMeshDetail->verts;
        params.detailVertsCount = iv.polyMeshDetail->nverts;
        params.detailTris = iv.polyMeshDetail->tris;
        params.detailTriCount = iv.polyMeshDetail->ntris;

		/// Offmesh connections
        params.offMeshConVerts = meshData.offMeshConnections.getCArray();
        params.offMeshConCount = meshData.offMeshConnections.size()/6;
        params.offMeshConRad = meshData.offMeshConnectionRads.getCArray();
        params.offMeshConDir = meshData.offMeshConnectionDirs.getCArray();
        params.offMeshConAreas = meshData.offMeshConnectionsAreas.getCArray();
        params.offMeshConFlags = meshData.offMeshConnectionsFlags.getCArray();

		/// Bah
        params.tileX = (((bmin[0] + bmax[0]) / 2) - navMesh->getParams()->orig[0]) / GRID_SIZE; // X
        params.tileY = (((bmin[2] + bmax[2]) / 2) - navMesh->getParams()->orig[2]) / GRID_SIZE; // Y
        rcVcopy(params.bmin, bmin);
        rcVcopy(params.bmax, bmax);

        params.walkableHeight = BASE_UNIT_DIM*config.walkableHeight;    // agent height
        params.walkableRadius = BASE_UNIT_DIM*config.walkableRadius;    // agent radius
        params.walkableClimb = BASE_UNIT_DIM*config.walkableClimb;      // keep less that walkableHeight (aka agent height)!
        params.cs = config.cs;
        params.ch = config.ch;
        params.tileSize = VERTEX_PER_MAP;

        // will hold final navmesh
        unsigned char* navData = NULL;
        int navDataSize = 0;

        do
        {
            // these values are checked within dtCreateNavMeshData - handle them here
            // so we have a clear error message
            if (params.nvp > DT_VERTS_PER_POLYGON)
            {
                printf("%s Invalid verts-per-polygon value! #:%04u \n", tileString, index);
                continue;
            }
            if (params.vertCount >= 0xffff)
            {
                printf("%s Too many vertices! #:%04u               \n", tileString, index);
                continue;
            }
            if (!params.vertCount || !params.verts)
            {
                // occurs mostly when adjacent tiles have models
                // loaded but those models don't span into this tile

                // message is an annoyance
                //printf("%sNo vertices to build tile!              \n", tileString);
                continue;
            }
            if (!params.polyCount || !params.polys ||
                    TILES_PER_MAP*TILES_PER_MAP == params.polyCount)
            {
                // we have flat tiles with no actual geometry - don't build those, its useless
                // keep in mind that we do output those into debug info
                // drop tiles with only exact count - some tiles may have geometry while having less tiles
                printf("%s No polygons to build on tile! #:%04u       \n", tileString, index);
                continue;
            }
            if (!params.detailMeshes || !params.detailVerts || !params.detailTris)
            {
                printf("%s No detail mesh to build tile! #:%04u    \n", tileString, index);
                continue;
            }

            printf("%s Building navmesh tile... #:%04u         \r", tileString, index);
            if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
            {
                printf("%s Failed building navmesh tile! #:%04u    \n", tileString, index);
                continue;
            }

            dtTileRef tileRef = 0;
            printf("%s Adding tile to navmesh... #:%04u         \r", tileString, index);
            // DT_TILE_FREE_DATA tells detour to unallocate memory when the tile
            // is removed via removeTile()
            dtStatus dtResult = navMesh->addTile(navData, navDataSize, DT_TILE_FREE_DATA, 0, &tileRef);
            if (!tileRef || dtResult != DT_SUCCESS)
            {
                printf("%s Failed adding tile to navmesh!(%u) #:%04u\n", tileString, (uint)dtResult, index);
                continue;
            }

            // file output
            char fileName[255];
            sprintf(fileName, "mmaps/%03u%02i%02i.mmtile", mapID, tileY, tileX);
            FILE* file = fopen(fileName, "wb");
            if (!file)
            {
                char message[1024];
                sprintf(message, "Failed to open %s for writing! #:%04u\n", fileName, index);
                perror(message);
                navMesh->removeTile(tileRef, NULL, NULL);
                continue;
            }

            printf("%s Writing to file... #:%04u               \r", tileString, index);

            // write header
            MmapTileHeader header;
            header.usesLiquids = m_terrainBuilder->usesLiquids();
            header.size = uint32(navDataSize);
            fwrite(&header, sizeof(MmapTileHeader), 1, file);

            // write data
            fwrite(navData, sizeof(unsigned char), navDataSize, file);
            fclose(file);

            // now that tile is written to disk, we can unload it
            navMesh->removeTile(tileRef, NULL, NULL);
        }
        while (0);

        if (m_debugOutput)
        {
            // restore padding so that the debug visualization is correct
            for (int i = 0; i < iv.polyMesh->nverts; ++i)
            {
                unsigned short* v = &iv.polyMesh->verts[i*3];
                v[0] += (unsigned short)config.borderSize;
                v[2] += (unsigned short)config.borderSize;
            }

            iv.generateObjFile(mapID, tileX, tileY, meshData);
            iv.writeIV(mapID, tileX, tileY);
        }
    }

    /**************************************************************************/
    void MapBuilder::getTileBounds(uint32 tileX, uint32 tileY, float* verts, int vertCount, float* bmin, float* bmax)
    {
        // this is for elevation
        if (verts && vertCount)
            rcCalcBounds(verts, vertCount, bmin, bmax);
        else
        {
            bmin[1] = FLT_MIN;
            bmax[1] = FLT_MAX;
        }

        // this is for width and depth
        bmax[0] = (32 - int(tileX)) * GRID_SIZE;
        bmax[2] = (32 - int(tileY)) * GRID_SIZE;
        bmin[0] = bmax[0] - GRID_SIZE;
        bmin[2] = bmax[2] - GRID_SIZE;
    }

    /**************************************************************************/
    bool MapBuilder::shouldSkipMap(uint32 mapID)
    {
        if (m_skipContinents)
        {
            switch (mapID)
            {
            case 0:
            case 1:
            case 530:
            case 571:
                return true;
            default:
                break;
            }
        }

        if (m_skipJunkMaps)
        {
            switch (mapID)
            {
            case 13:    // test.wdt
            case 25:    // ScottTest.wdt
            case 29:    // Test.wdt
            case 42:    // Colin.wdt
            case 169:   // EmeraldDream.wdt (unused, and very large)
            case 451:   // development.wdt
            case 573:   // ExteriorTest.wdt
            case 597:   // CraigTest.wdt
            case 605:   // development_nonweighted.wdt
            case 606:   // QA_DVD.wdt
                return true;
            default:
                if (isTransportMap(mapID))
                    return true;
                break;
            }
        }

        if (m_skipBattlegrounds)
        {
            switch (mapID)
            {
            case 30:    // AV
            case 37:    // ?
            case 489:   // WSG
            case 529:   // AB
            case 566:   // EotS
            case 607:   // SotA
            case 628:   // IoC
                return true;
            default:
                break;
            }
        }

        return false;
    }

    /**************************************************************************/
    bool MapBuilder::isTransportMap(uint32 mapID)
    {
        switch (mapID)
        {
            // transport maps
            case 582:
            case 584:
            case 586:
            case 587:
            case 588:
            case 589:
            case 590:
            case 591:
            case 592:
            case 593:
            case 594:
            case 596:
            case 610:
            case 612:
            case 613:
            case 614:
            case 620:
            case 621:
            case 622:
            case 623:
            case 641:
            case 642:
            case 647:
            case 672:
            case 673:
            case 712:
            case 713:
            case 718:
                return true;
            default:
                return false;
        }
    }

    /**************************************************************************/
    bool MapBuilder::shouldSkipTile(uint32 mapID, uint32 tileX, uint32 tileY)
    {
        char fileName[255];
        sprintf(fileName, "mmaps/%03u%02i%02i.mmtile", mapID, tileY, tileX);
        FILE* file = fopen(fileName, "rb");
        if (!file)
            return false;

        MmapTileHeader header;
        fread(&header, sizeof(MmapTileHeader), 1, file);
        fclose(file);

        if (header.mmapMagic != MMAP_MAGIC || header.dtVersion != DT_NAVMESH_VERSION)
            return false;

        if (header.mmapVersion != MMAP_VERSION)
            return false;

        return true;
    }

}
