#pragma once

#include "IPhotoPrintMaker.h"
#include "PhotoStandard.h"
#include "CanvasDefinition.h"

class PhotoPrintMaker : public IPhotoPrintMaker
{
public:
    virtual cv::Mat cropPicture(const cv::Mat& originalImage,
        const cv::Point& crownPoint,
        const cv::Point& chinPoint,
        const PhotoStandard& ps) override;

    // Creates a tiled photo from the cropped photo
    virtual cv::Mat tileCroppedPhoto(const CanvasDefinition& canvas,
        const PhotoStandard& ps,
        const cv::Mat& croppedImage) override;

private:
    cv::Point2d centerCropEstimation(const PhotoStandard &ps,
        const cv::Point& crownPoint,
        const cv::Point& chinPoint);
};