#include "precomp.hpp"
#include "_lsvmc_parser.h"
#include "_lsvmc_matching.h"
namespace cv
{
namespace lsvmcascade
{

const int pca_size = 31;

/*
// load trained detector from a file
//
// API
// CvLatentSvmDetectorCaskad* cvLoadLatentSvmDetector(const char* filename);
// INPUT
// filename             - path to the file containing the parameters of
//                      - trained Latent SVM detector
// OUTPUT
// trained Latent SVM detector in internal representation
*/
CvLatentSvmDetectorCaskad* cvLoadLatentSvmDetectorCaskad(const char* filename)
{
    CvLatentSvmDetectorCaskad* detector = 0;
    CvLSVMFilterObjectCaskad** filters = 0;
    int kFilters = 0;
    int kComponents = 0;
    int* kPartFilters = 0;
    float* b = 0;
    float scoreThreshold = 0.f;
    int err_code = 0;
	float* PCAcoeff = 0;

    err_code = loadModel(filename, &filters, &kFilters, &kComponents, &kPartFilters, &b, &scoreThreshold, &PCAcoeff);
    if (err_code != LATENT_SVM_OK) return 0;

    detector = (CvLatentSvmDetectorCaskad*)malloc(sizeof(CvLatentSvmDetectorCaskad));
    detector->filters = filters;
    detector->b = b;
    detector->num_components = kComponents;
    detector->num_filters = kFilters;
    detector->num_part_filters = kPartFilters;
    detector->score_threshold = scoreThreshold;
	  detector->pca = PCAcoeff;
    detector->pca_size = pca_size;

    return detector;
}

/*
// release memory allocated for CvLatentSvmDetectorCaskad structure
//
// API
// void cvReleaseLatentSvmDetector(CvLatentSvmDetectorCaskad** detector);
// INPUT
// detector             - CvLatentSvmDetectorCaskad structure to be released
// OUTPUT
*/
CVAPI(void) cvReleaseLatentSvmDetectorCaskad(CvLatentSvmDetectorCaskad** detector)
{
    free((*detector)->b);
    free((*detector)->num_part_filters);
    for (int i = 0; i < (*detector)->num_filters; i++)
    {
        free((*detector)->filters[i]->H);
        free((*detector)->filters[i]);
    }
    free((*detector)->filters);
	free((*detector)->pca);
    free((*detector));
    *detector = 0;
}

/*
// find rectangular regions in the given image that are likely
// to contain objects and corresponding confidence levels
//
// API
// CvSeq* cvLatentSvmDetectObjects(const IplImage* image,
//                                  CvLatentSvmDetectorCaskad* detector,
//                                  CvMemStorage* storage,
//                                  float overlap_threshold = 0.5f);
// INPUT
// image                - image to detect objects in
// detector             - Latent SVM detector in internal representation
// storage              - memory storage to store the resultant sequence
//                          of the object candidate rectangles
// overlap_threshold    - threshold for the non-maximum suppression algorithm [here will be the reference to original paper]
// OUTPUT
// sequence of detected objects (bounding boxes and confidence levels stored in CvObjectDetection structures)
*/
CvSeq* cvLatentSvmDetectObjectsCaskad(IplImage* image,
                                CvLatentSvmDetectorCaskad* detector,
                                CvMemStorage* storage,
                                float overlap_threshold)
{
    CvLSVMFeaturePyramidCaskad *H = 0;
	CvLSVMFeaturePyramidCaskad *H_PCA = 0;
    CvPoint *points = 0, *oppPoints = 0;
    int kPoints = 0;
    float *score = 0;
    unsigned int maxXBorder = 0, maxYBorder = 0;
    int numBoxesOut = 0;
    CvPoint *pointsOut = 0;
    CvPoint *oppPointsOut = 0;
    float *scoreOut = 0;
    CvSeq* result_seq = 0;
    int error = 0;

    if(image->nChannels == 3)
        cvCvtColor(image, image, CV_BGR2RGB);

    // Getting maximum filter dimensions
    getMaxFilterDims((const CvLSVMFilterObjectCaskad**)(detector->filters), detector->num_components,
                     detector->num_part_filters, &maxXBorder, &maxYBorder);
    // Create feature pyramid with nullable border
    H = createFeaturePyramidWithBorder(image, maxXBorder, maxYBorder);
	
	// Create PSA feature pyramid
    H_PCA = createPCA_FeaturePyramid(H, detector, maxXBorder, maxYBorder);
    
    FeaturePyramid32(H, maxXBorder, maxYBorder);
	
    // Search object
    error = searchObjectThresholdSomeComponents(H, H_PCA,(const CvLSVMFilterObjectCaskad**)(detector->filters),
        detector->num_components, detector->num_part_filters, detector->b, detector->score_threshold,
        &points, &oppPoints, &score, &kPoints);
    if (error != LATENT_SVM_OK)
    {
        return NULL;
    }
    // Clipping boxes
    clippingBoxes(image->width, image->height, points, kPoints);
    clippingBoxes(image->width, image->height, oppPoints, kPoints);
    // NMS procedure
    nonMaximumSuppression(kPoints, points, oppPoints, score, overlap_threshold,
                &numBoxesOut, &pointsOut, &oppPointsOut, &scoreOut);

    result_seq = cvCreateSeq( 0, sizeof(CvSeq), sizeof(CvObjectDetection), storage );

    for (int i = 0; i < numBoxesOut; i++)
    {
        CvObjectDetection detection = {{0, 0, 0, 0}, 0};
        detection.score = scoreOut[i];
        CvRect bounding_box = {0, 0, 0, 0};
        bounding_box.x = pointsOut[i].x;
        bounding_box.y = pointsOut[i].y;
        bounding_box.width = oppPointsOut[i].x - pointsOut[i].x;
        bounding_box.height = oppPointsOut[i].y - pointsOut[i].y;
        detection.rect = bounding_box;
        cvSeqPush(result_seq, &detection);
    }

    if(image->nChannels == 3)
        cvCvtColor(image, image, CV_RGB2BGR);

    freeFeaturePyramidObject(&H);
	freeFeaturePyramidObject(&H_PCA);
    free(points);
    free(oppPoints);
    free(score);

    return result_seq;
}

LatentSvmDetector::ObjectDetection::ObjectDetection() : score(0.f), classID(-1)
{}

LatentSvmDetector::ObjectDetection::ObjectDetection( const Rect& _rect, float _score, int _classID ) :
    rect(_rect), score(_score), classID(_classID)
{}

LatentSvmDetector::LatentSvmDetector()
{}

LatentSvmDetector::LatentSvmDetector( const vector<string>& filenames, const vector<string>& _classNames )
{
    load( filenames, _classNames );
}

LatentSvmDetector::~LatentSvmDetector()
{
    clear();
}

void LatentSvmDetector::clear()
{
    for( size_t i = 0; i < detectors.size(); i++ )
      cv::lsvmcascade::cvReleaseLatentSvmDetectorCaskad( &detectors[i] );
    detectors.clear();

    classNames.clear();
}

bool LatentSvmDetector::empty() const
{
    return detectors.empty();
}

const vector<string>& LatentSvmDetector::getClassNames() const
{
    return classNames;
}

size_t LatentSvmDetector::getClassCount() const
{
    return classNames.size();
}

string extractModelName( const string& filename )
{
    size_t startPos = filename.rfind('/');
    if( startPos == string::npos )
        startPos = filename.rfind('\\');

    if( startPos == string::npos )
        startPos = 0;
    else
        startPos++;

    const int extentionSize = 4; //.xml

    int substrLength = (int)(filename.size() - startPos - extentionSize);

    return filename.substr(startPos, substrLength);
}

bool LatentSvmDetector::load( const vector<string>& filenames, const vector<string>& _classNames )
{
    clear();

    CV_Assert( _classNames.empty() || _classNames.size() == filenames.size() );

    for( size_t i = 0; i < filenames.size(); i++ )
    {
        const string filename = filenames[i];
        if( filename.length() < 5 || filename.substr(filename.length()-4, 4) != ".xml" )
            continue;

        CvLatentSvmDetectorCaskad* detector = cvLoadLatentSvmDetectorCaskad( filename.c_str() );
        if( detector )
        {
            detectors.push_back( detector );
            if( _classNames.empty() )
            {
                classNames.push_back( extractModelName(filenames[i]) );
            }
            else
                classNames.push_back( _classNames[i] );
        }
    }

    return !empty();
}

void LatentSvmDetector::detect( const Mat& image,
                                vector<ObjectDetection>& objectDetections,
                                float overlapThreshold)
{
    objectDetections.clear();
    
    for( size_t classID = 0; classID < detectors.size(); classID++ )
    {
        IplImage image_ipl = image;
        CvMemStorage* storage = cvCreateMemStorage(0);
        CvSeq* detections = cv::lsvmcascade::cvLatentSvmDetectObjectsCaskad( &image_ipl, (CvLatentSvmDetectorCaskad*)(detectors[classID]), storage, overlapThreshold);

        // convert results
        objectDetections.reserve( objectDetections.size() + detections->total );
        for( int detectionIdx = 0; detectionIdx < detections->total; detectionIdx++ )
        {
            CvObjectDetection detection = *(CvObjectDetection*)cvGetSeqElem( detections, detectionIdx );
            objectDetections.push_back( ObjectDetection(Rect(detection.rect), detection.score, (int)classID) );
        }

        cvReleaseMemStorage( &storage );
    }
}

} // namespace cv
}
