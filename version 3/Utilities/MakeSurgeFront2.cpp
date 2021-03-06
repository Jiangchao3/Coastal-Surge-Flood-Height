//
// Created by a1091793 on 15/09/17.
//

//
// Created by a1091793 on 15/9/17.
//

#include <iostream>
#include <fstream>

#include <ogrsf_frmts.h>

#include <blink/raster/gdal_raster.h>
#include <blink/raster/utility.h> // To open rasters
#include <blink/iterator/zip_range.h>

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/progress.hpp>
#include <boost/program_options.hpp>

#include "../Types.h"
#include <boost/graph/graph_traits.hpp>
#include "../Pathify.hpp"
#include "ReadGraphsFromFile.h"
#include "PrintGraphsToFile.h"



struct SurgeFrontParameters
{
    SurgeFrontParameters()
    {

    }

    CmdLinePaths coast_boundry_grph_file;
    CmdLinePaths out_surge_front_grph_file;
    CmdLinePaths surge_level_controls;

    CmdLinePaths template_raster_file;
    CmdLinePaths surge_front_raster_file;

    std::string field_name;
//    std::string id_name;
};

std::pair<std::string, std::string> at_option_parser(std::string const&s)
{
    if ('@' == s[0])
        return std::make_pair(std::string("cfg-file"), s.substr(1));
    else
        return std::pair<std::string, std::string>();
}





void
processCmdLineArguments(int argc, char* argv[], SurgeFrontParameters & _params) throw(std::runtime_error)
{
    namespace prog_opt = boost::program_options;
    prog_opt::options_description desc("Allowed options");
    desc.add_options()
            ("help,h", "produce help message")
            ("input-graph,i", prog_opt::value<CmdLinePaths>(&_params.coast_boundry_grph_file),
             "path of the graphml representation of coastal boundary")
            ("output-name,o", prog_opt::value<CmdLinePaths>(&_params.out_surge_front_grph_file),
             "path of the graphml represenation of coastal boundary with surge levels")
            ("surge-levels,s", prog_opt::value<CmdLinePaths>(&_params.surge_level_controls),
             "path of the shape file with surge levels")
            ("field-name,f", prog_opt::value<std::string>(&_params.field_name),
             "name of shape file field to get surge height from")
            ("template-raster,t", prog_opt::value<CmdLinePaths>(&_params.template_raster_file),
             "Path to template raster to base surge front raster on")

//            ("id-name,n", prog_opt::value<std::string>(&_params.id_name),
//             "name of shape file field that is unique identifier for each point")
            ("cfg-file,g", prog_opt::value<std::string>(),
             "can be specified with '@name', too")
            ;

    prog_opt::variables_map vm;
    prog_opt::store(prog_opt::command_line_parser(argc, argv).options(desc).extra_parser(at_option_parser).run(), vm);

    if (vm.count("help"))
    {
        std::cout << desc << "\n";
        return;
    }

    if (vm.count("cfg-file"))
    {
        // Load the file and tokenize it
        std::string cfg_file_path = vm["cfg-file"].as<std::string>();
        std::ifstream ifs(cfg_file_path.c_str());
        if (!ifs)
        {
            std::stringstream ss;
            ss << "Could not open the cfg file: " << cfg_file_path;
            throw std::runtime_error(ss.str());
        }
        prog_opt::store(prog_opt::parse_config_file(ifs, desc), vm);
    }

    prog_opt::notify(vm);

    try{
        pathify(_params.coast_boundry_grph_file);
    }
    catch (boost::filesystem::filesystem_error & err)
    {
        std::stringstream ss;
        ss << "Filesystem error: " << err.what();
        throw std::runtime_error(ss.str());
    }

    try{
        pathify(_params.surge_level_controls);
    }
    catch (boost::filesystem::filesystem_error & err)
    {
        std::stringstream ss;
        ss << "Filesystem error: " << err.what();
        throw std::runtime_error(ss.str());
    }

    try{
        pathify(_params.template_raster_file);
    }
    catch (boost::filesystem::filesystem_error & err)
    {
        std::stringstream ss;
        ss << "Filesystem error: " << err.what();
        throw std::runtime_error(ss.str());
    }

    try{
        pathify_mk(_params.out_surge_front_grph_file);
    }
    catch (boost::filesystem::filesystem_error & err)
    {
        std::stringstream ss;
        ss << "Filesystem error: " << err.what();
        throw std::runtime_error(ss.str());
    }

    _params.surge_front_raster_file.first = _params.out_surge_front_grph_file.first + ".tif";
    try{
        pathify_mk(_params.surge_front_raster_file);
    }
    catch (boost::filesystem::filesystem_error & err)
    {
        std::stringstream ss;
        ss << "Filesystem error: " << err.what();
        throw std::runtime_error(ss.str());
    }

}


int main(int argc, char* argv[])
{
    namespace raster_util = blink::raster;
    typedef raster_util::coordinate_2d Coord;

    SurgeFrontParameters params;
    processCmdLineArguments(argc, argv, params);

    /**********************************/
    /*       Create graph object      */
    /**********************************/
    Graph channel_grph;

    /**********************************/
    /*         Read in Graph           */
    /**********************************/
    std::cout << "\n\n*************************************\n";
    std::cout <<     "*             Read in Graphs        *\n";
    std::cout <<     "*************************************" << std::endl;
    //    readGraphFromFile(control_graph_path, control_grph);
    readGraphFromFile(params.coast_boundry_grph_file.second, channel_grph);

    /**********************************/
    /*  Read in Controls shape file   */
    /**********************************/
    std::cout << "\n\n*************************************\n";
    std::cout <<     "*         Read in Shape file        *\n";
    std::cout <<     "*************************************" << std::endl;
    //    readGraphFromFile(control_graph_path, control_grph);
    GDALAllRegister();
    GDALDataset * poDS;
    poDS = (GDALDataset*) GDALOpenEx(params.surge_level_controls.first.c_str(), GDAL_OF_VECTOR, NULL, NULL, NULL);
    if( poDS == NULL )
    {
        printf( "Open failed.\n" );
        return EXIT_FAILURE;
    }
    OGRLayer *poLayer;
//    int num_layers = poDS->GetLayerCount();
    poLayer = poDS->GetLayer(0);
    poLayer->ResetReading();
    OGRFeatureDefn *poFDefn = poLayer->GetLayerDefn();
    int field_index = poFDefn->GetFieldIndex(params.field_name.c_str());
//    int id_index = poFDefn->GetFieldIndex(params.id_name.c_str());

    /**********************************/
    /*     Find closest controls      */
    /**********************************/
    std::cout << "\n\n*************************************\n";
    std::cout <<     "*      Finding closest controls     *\n";
    std::cout <<     "*************************************" << std::endl;
    VertexDescMap idxMap;
    VertexIDMap idMap;
    boost::associative_property_map<VertexDescMap> indexMap(idxMap);
    VertexIterator di, dj;
    boost::tie(di, dj) = boost::vertices(channel_grph);
    OGRGeometry *poGeometry;

    std::map<GIntBig, std::pair<VertexDescriptor, double> > closest_coast_pixel_map;

    OGRFeature *poFeature;
    poLayer->ResetReading();

    boost::progress_display show_progress2(boost::num_vertices(channel_grph));

    //while( (poFeature = poLayer->GetNextFeature()) != NULL )
    //{
    //    closest_coast_pixel_map[poFeature->GetFID()] = std::make_pair(&(channel_grph[*di]), std::numeric_limits<double>::max());
    //}

	struct CloseControls
	{
		double dist = std::numeric_limits<double>::max();
		GIntBig control;
	};

    for(; di != dj; ++di)
    {
		CloseControls nearest, second_nearest;
        poLayer->ResetReading();
        OGRFeature * poFeature;
        while( (poFeature = poLayer->GetNextFeature()) != NULL )
        {
            poGeometry = poFeature->GetGeometryRef();
            if( poGeometry != NULL
                && wkbFlatten(poGeometry->getGeometryType()) == wkbPoint )
            {
                OGRPoint *poPoint = (OGRPoint *) poGeometry;
                double control_x = poPoint->getX();
                double control_y = poPoint->getY();
                double raster_x = channel_grph[*di].x_coord;
                double raster_y = channel_grph[*di].y_coord;
                double dist = std::sqrt(std::pow(control_x-raster_x,2) + std::pow(control_y-raster_y,2));

				if (dist < nearest.dist)
				{
					nearest.dist = dist;
					nearest.control = poFeature->GetFID();
				}
				else if (dist < second_nearest.dist)
				{
					second_nearest.dist = dist;
					second_nearest.control = poFeature->GetFID();
				}

            }            
        }
        ++show_progress2;
		channel_grph[*di].down_cntrl_id = nearest.control;
		channel_grph[*di].down_cntrl_dist = nearest.dist;
		channel_grph[*di].up_cntrl_id = second_nearest.control;
		channel_grph[*di].up_cntrl_dist = second_nearest.dist;
		OGRFeature::DestroyFeature(poFeature);
    }

    std::cout << "\n\n*************************************\n";
    std::cout <<     "*       Linearly interpolating      *\n";
    std::cout <<     "*************************************" << std::endl;
    /****************************************************/
    /*  Linearly interpolate all channel pixel levels   */
    /****************************************************/


    boost::progress_display show_progress4(boost::num_vertices(channel_grph));
	std::pair<VertexIterator, VertexIterator> vp;
    for (vp = boost::vertices(channel_grph); vp.first != vp.second; ++vp.first)
    {
        ChannelNode & node = channel_grph[*vp.first];
        if (node.type <= 1)
        {
            double up_dist = 1.0 / double(node.up_cntrl_dist);
            double down_dist = 1.0 / double(node.down_cntrl_dist);
            if (up_dist != -1 && down_dist != -1) {
                VertexDescriptor down_node = idMap[node.down_cntrl_id];
                VertexDescriptor up_node = idMap[node.up_cntrl_id];
                double up_level = poLayer->GetFeature(node.up_cntrl_id)->GetFieldAsDouble(field_index);
                double down_level = poLayer->GetFeature(node.down_cntrl_id)->GetFieldAsDouble(field_index);
                node.level = (down_dist * down_level
                              + up_dist * up_level)
                             / (up_dist + down_dist);
            }
        }

        ++show_progress4;
    }

    /********************************************/
    /*       Print Channel graphs to file       */
    /********************************************/
    std::cout << "\n\n*************************************\n";
    std::cout <<     "*  Printing channel Graph to file   *\n";
    std::cout <<     "*************************************" << std::endl;
    printGraphsToFile(channel_grph, params.out_surge_front_grph_file.first);

    /********************************************/
    /*       Print Channel graphs to SVG       */
    /********************************************/
    std::cout << "\n\n*************************************\n";
    std::cout <<     "*  Printing channel Graph to SVG   *\n";
    std::cout <<     "*************************************" << std::endl;
    printGraphToSVG(channel_grph, params.out_surge_front_grph_file.first);

    std::cout << "\n\n*************************************\n";
    std::cout << "*      Setting up output raster     *\n";
    std::cout << "*************************************" << std::endl;
    boost::shared_ptr<raster_util::gdal_raster<float> > template_map = raster_util::open_gdal_rasterSPtr<float>(params.template_raster_file.second, GA_ReadOnly);
    boost::shared_ptr<raster_util::gdal_raster<float> > output_map = raster_util::create_gdal_rasterSPtr_from_model<float, float>(params.surge_front_raster_file.second, *template_map, GDT_Float32);
    float out_no_data_val = 0.0;
    const_cast<GDALRasterBand *>(output_map->get_gdal_band())->SetNoDataValue(out_no_data_val);

    int cols = output_map->nCols();
    int rows = output_map->nRows();
    unsigned long num_cells = cols;
    num_cells *= rows;
    boost::progress_display show_progress1(rows);

    for (unsigned int i = 0; i < rows; ++i) {
		++show_progress1;
        for (unsigned int j = 0; j < cols; ++j) {            
            output_map->put(Coord(i, j),out_no_data_val);
        }
    }

    std::cout << "\n\n*************************************\n";
    std::cout << "*      Saving levels to output raster     *\n";
    std::cout << "*************************************" << std::endl;

    for (vp = boost::vertices(channel_grph); vp.first != vp.second; ++vp.first)
    {
        ChannelNode & node = channel_grph[*vp.first];
        output_map->put(Coord(node.row, node.col), node.level);
    }



}