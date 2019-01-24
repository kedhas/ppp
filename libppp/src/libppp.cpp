#include "libppp.h"
#include "CanvasDefinition.h"
#include "CommonHelpers.h"
#include "LandMarks.h"
#include "PhotoStandard.h"
#include "PppEngine.h"
#include "Utilities.h"

#include <regex>

#include <opencv2/imgcodecs.hpp>

using namespace std;

PublicPppEngine g_c_pppInstance;
string g_last_error;

cv::Point fromJson(rapidjson::Value & v)
{
    return cv::Point(v["x"].GetInt(), v["y"].GetInt());
}

PublicPppEngine::PublicPppEngine()
: m_pPppEngine(new PppEngine)
{
}

PublicPppEngine::~PublicPppEngine()
{
    delete m_pPppEngine;
}

bool PublicPppEngine::configure(const char * jsonConfig) const
{
    rapidjson::Document parser;
    parser.Parse(jsonConfig);
    return m_pPppEngine->configure(parser);
}

std::string PublicPppEngine::setImage(const char * bufferData, size_t bufferLength) const
{
    cv::Mat inputImage;
    if (bufferLength <= 0)
    {
        // Find out if this is a data url
        auto offset = 0;
        auto dataLen = strlen(bufferData);

        regex re("^data:([a-z]+\\/[a-z]+(;[a-z\\-]+\\=[a-z\\-]+)?)?(;base64)?,");
        std::cmatch cm;    // same as std::match_results<const char*> cm;
        if (std::regex_search (bufferData, cm, re)) {
            offset = cm[0].length();
            dataLen -= offset;
        }

        auto decodedBytes = Utilities::base64Decode(bufferData + offset, dataLen);
        cv::_InputArray inputArray(decodedBytes.data(), static_cast<int>(decodedBytes.size()));
        inputImage = cv::imdecode(inputArray, cv::IMREAD_COLOR);
    }
    else
    {
        cv::_InputArray inputArray(bufferData, static_cast<int>(bufferLength));
        inputImage = cv::imdecode(inputArray, cv::IMREAD_COLOR);
    }
    return m_pPppEngine->setInputImage(inputImage);
}

std::string PublicPppEngine::detectLandmarks(const std::string & imageId) const
{
    LandMarks landMarks;
    m_pPppEngine->detectLandMarks(imageId, landMarks);
    return landMarks.toJson();
}

std::string PublicPppEngine::createTiledPrint(const std::string & imageId, const std::string & request) const
{
    rapidjson::Document d;
    d.Parse(request.c_str());

    auto ps = PhotoStandard::fromJson(d["standard"]);
    auto canvas = CanvasDefinition::fromJson(d["canvas"]);
    auto cronwPoint = fromJson(d["crownPoint"]);
    auto chinPoint = fromJson(d["chinPoint"]);
    auto asBase64Encode = false;

    if (d.HasMember("asBase64"))
    {
        asBase64Encode = d["asBase64"].GetBool();
    }

    auto result = m_pPppEngine->createTiledPrint(imageId, *ps, *canvas, cronwPoint, chinPoint);

    std::vector<BYTE> pictureData;
    cv::imencode(".png", result, pictureData);

    // Add image resolution to output
    setPngResolutionDpi(pictureData, canvas->resolutionPixelsPerMM());

    if (asBase64Encode)
    {
        return Utilities::base64Encode(pictureData);
    }
    return std::string(pictureData.begin(), pictureData.end());
}

template <typename T>
std::vector<BYTE> toBytes(const T & x)
{
    vector<BYTE> v(static_cast<const BYTE *>(static_cast<const void *>(&x)),
                   static_cast<const BYTE *>(static_cast<const void *>(&x)) + sizeof(x));
    reverse(v.begin(), v.end()); // Little endian notation
    return v;
}

/*  The pHYs chunk specifies the intended pixel size or aspect ratio for display of the image. It contains:
    Pixels per unit, X axis: 4 bytes (unsigned integer)
    Pixels per unit, Y axis: 4 bytes (unsigned integer)
    Unit specifier:          1 byte
The following values are defined for the unit specifier:
    0: unit is unknown
    1: unit is the meter
pHYs has to go before IDAT chunk
*/
void PublicPppEngine::setPngResolutionDpi(std::vector<BYTE> & imageStream, double resolution_ppmm)
{
    auto chunkLenBytes = toBytes(9);
    auto resolBytes = toBytes(ROUND_INT(resolution_ppmm * 1000));
    const string physStr = "pHYs";

    auto pHYsChunk(chunkLenBytes);
    pHYsChunk.insert(pHYsChunk.end(), physStr.begin(), physStr.end());

    pHYsChunk.insert(pHYsChunk.end(), resolBytes.begin(), resolBytes.end());
    pHYsChunk.insert(pHYsChunk.end(), resolBytes.begin(), resolBytes.end());
    pHYsChunk.push_back(1); // Unit is the meter

    auto crcBytes = toBytes(Utilities::crc32(0, &pHYsChunk[4], &pHYsChunk[4] + pHYsChunk.size() - 4));
    pHYsChunk.insert(pHYsChunk.end(), crcBytes.begin(), crcBytes.end());

    string idat = "IDAT";
    auto it = search(imageStream.begin(), imageStream.end(), idat.begin(), idat.end());

    if (it != imageStream.end())
    {
        // Insert the chunk in the stream
        imageStream.insert(it - 4, pHYsChunk.begin(), pHYsChunk.end());
    }
}

#pragma region C Interface
#define TRYRUN(statements)                                                                                             \
    try                                                                                                                \
    {                                                                                                                  \
        statements;                                                                                                    \
        return true;                                                                                                   \
    }                                                                                                                  \
    catch (const std::exception & ex)                                                                                  \
    {                                                                                                                  \
        g_last_error = ex.what();                                                                                      \
        return false;                                                                                                  \
    }

bool set_image(const char * img_buf, int img_buf_size, char * img_id)
{
    TRYRUN(auto imgId = g_c_pppInstance.setImage(img_buf, img_buf_size); strcpy(img_id, imgId.c_str()););
}

bool configure(const char * config_json)
{
    TRYRUN(g_c_pppInstance.configure(config_json););
}

bool detect_landmarks(const char * img_id, char * landmarks)
{
    TRYRUN(auto landmarksStr = g_c_pppInstance.detectLandmarks(img_id); strcpy(landmarks, landmarksStr.c_str()););
}

int create_tiled_print(const char * img_id, const char * request, char * out_buf)
{
    try
    {
        auto output = g_c_pppInstance.createTiledPrint(img_id, request);
        const auto out_size = static_cast<int>(output.size());
        copy(output.begin(), output.end(), out_buf);
        return out_size;
    }
    catch (const std::exception & ex)
    {
        g_last_error = ex.what();
        return 0;
    }
}

#pragma endregion

#ifdef EMSCRIPTEN

#include <stdio.h>
#include <emscripten.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// global data
cv::Mat3b bgr_g, bgr_out_g;

using namespace cv;

extern "C"
{

   bool EMSCRIPTEN_KEEPALIVE rotate_colors(int width, int height,
                                           cv::Vec4b* frame4b_ptr,
                                           cv::Vec4b* frame4b_ptr_out,
                                           int hsteps) try
   {
      // wrap memory pointers with proper cv::Mat images (no copies)
      cv::Mat4b rgba_in(height, width, frame4b_ptr);
      cv::Mat4b rgba_out(height, width, frame4b_ptr_out);

      // allocate 3-channel images if needed
      bgr_g.create(rgba_in.size());
      bgr_out_g.create(rgba_in.size());

      cv::cvtColor(rgba_in, bgr_g, cv::COLOR_RGBA2BGR);
      // color_cycle::rotate_hue(bgr_g, bgr_out_g, hsteps);

      // mix BGR + A (from input) => RGBA output
      const Mat in_mats[] = { bgr_out_g, rgba_in };
      constexpr int from_to[] = { 0,2, 1,1, 2,0, 6,3 };
      mixChannels(in_mats, std::size(in_mats), &rgba_out, 1, from_to, std::size(from_to)/2);
      return true;
   }
   catch (std::exception const& e)
   {
      printf("Exception thrown: %s\n", e.what());
      return false;
   }
   catch (...)
   {
      printf("Unknown exception thrown!\n");
      return false;
   }

   void EMSCRIPTEN_KEEPALIVE release()
   {
      //color_cycle::clear_all();
      bgr_g.release();
      bgr_out_g.release();
   }
}

#endif
