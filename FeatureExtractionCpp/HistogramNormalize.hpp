#pragma once
#include "TransformImage.hpp"

#pragma warning(disable: 4244)
const Channels default_channels[3] = { Channels::H, Channels::S, Channels::V };

// histogram specification & equalization for color images
class HistogramNormalize
{

private:
    TransformImage _refImage;
    gpu::GpuMat g_hist;
    Mat _refHist;
    Mat _refHist_CV_32F;

    const vector<Channels> channels;
    bool _hasCalcedHist = false;
    bool _hasCalcedFreqHist = false;

    void CalcRefHistogram(Channels channel)
    {
        if ((_hasCalcedHist && _hasCalcedFreqHist) || (_hasCalcedFreqHist && channel == Channels::ALL) || (_hasCalcedHist && channel != Channels::ALL))
        {
            return;
        }

        CalcHist(_refImage, _refHist, channel);
        _hasCalcedHist = channel != Channels::ALL;
        _hasCalcedFreqHist = channel == Channels::ALL;
    }

    template<typename T>
    void ImageMap(Mat& image, Mat& dest, Mat& mapping)
    {

        for (int i = 0; i < image.rows; i++)
        {
            for (int j = 0; j < image.cols; j++)
            {
                dest.at<T>(i, j) = mapping.at<T>(0, image.at<T>(i, j));
            }
        }

    }
public:
    HistogramNormalize(Mat refImage) : _refImage(refImage), channels(default_channels, default_channels + 3)
    { 
        
    }

    // calculate the histogram for the entire image
    // with "flattened" values of the 3 channels
    void CalcChannelHist(Mat& img, Mat& hist, int normConst = 0xFF)
    {
        Mat buf;
        if (normConst == 0xFFFFFF)
        {
            img.convertTo(buf, CV_32FC1);
        }
        else
        {
            buf = img;
        }

        float ranges[] = { 0, normConst };
        const float* pranges[] = { ranges };
        int bins =  normConst + 1;
        int histSize[] = { bins };
        int channels[] = { 0 };

        calcHist(&buf, 1, channels, Mat(), hist, 1, histSize, pranges);
    }

    void CalcHist(TransformImage& ti, Mat& hist, Channels channel)
    {
        Mat buf;
        int normConst = channel == Channels::ALL ? 0xFFFFFF : 0xFF;

        if (channel == Channels::ALL)
        {
            CalcChannelHist(ti.getEnhanced(), buf);
        }
        else
        {
            ti.GetOneChannelImages(channel);
            gpu::GpuMat gbuf;

            //REVIEW: OpenCV V range is 0 - 255, so simple call to GetHist
            // which is not parameterized should be fine
            ti.GetHist(gbuf);
            gbuf.download(buf);
        }

        integral(buf, hist);
        normalize(hist, hist, 0., 255., NORM_MINMAX);
    }

    // map inpHist to refHist (in img and ref):
    // |img[i] - ref[j]| = min(k) |img[i] - ref[k]|
    template<typename T>
    void CreateHistMap(Mat& ref, Mat& img, Mat& dst)
    {
        dst = Mat::zeros(ref.size(), CV_8UC1);

        for (int i = 0; i < img.cols; i++)
        {
            long curMin = img.at<long>(0, i) - ref.at<long>(0, 0);
            for (int j = 1; j < ref.cols; j++)
            {
                int diff = std::abs(img.at<long>(0, i) - ref.at<long>(0, j));
                if (diff < curMin)
                {
                    curMin = diff;
                    dst.at<uchar>(0, i) = (uchar)j;
                }
            }
        }
    }

    // Algorithm described here:
    // http://fourier.eng.hmc.edu/e161/lectures/contrast_transform/node3.html
    // if freq is true, ignore channel
    void HistogramSpecification(Mat& image, Mat& dest, Channels channel)
    {
        //1. Get the reference image histogram (cumulative, normalized)
        CalcRefHistogram(channel);

        TransformImage ti(image);
        Mat hist;

        //2. Get the input image histogram
        CalcHist(ti, hist, channel);
        Mat mapping;
        
        //3. Create the mapping
        if (channel == Channels::ALL)
        {
            CreateHistMap<float>(_refHist, hist, mapping);
        }
        else
        {
            CreateHistMap<uchar>(_refHist, hist, mapping);
        }

        //4 actually map the pixels
        dest = Mat::zeros(image.rows, image.cols, channel == Channels::ALL ? CV_32FC1 : CV_8UC1);
        Mat oneChannel;
        if (channel == Channels::ALL)
        {
            ti.GetOneChannelImages(channel).download(oneChannel);
        }
        else
        {
            oneChannel = image;
        }

        if (channel == Channels::ALL)
        {
            ImageMap<float>(oneChannel, dest, mapping);
        }
        else
        { 
            ImageMap<uchar>(oneChannel, dest, mapping);
        }

    }
};