import numpy as np
from kobra.dr import KNeighborsRegions, Labels, ExtractBloodVessels, ImageReader
import mahotas as mh
from kobra.imaging import max_labelled_region
import cv2

# path to the images preprocessed by histogram specification from 16_left.jpeg
root = '/kaggle/retina/train/labelled'
annotations = 'annotations.txt'
masks_dir = '/kaggle/retina/train/masks'
im_file = '4/16_left.jpeg'

# path to the original images
orig_path = '/kaggle/retina/train/sample/split'

class DarkBrightDetector(KNeighborsRegions):
    '''
    Detect bright/dark spots.
    Drusen/Exudates (bright) and haemorages/aneurisms (dark) detected
    '''
    def __init__(self, root, orig_path, im_file, annotations, masks_dir, n_neighbors = 3):

        KNeighborsRegions.__init__(self, root, im_file, annotations, masks_dir, n_neighbors)
        self._prediction = np.array([])
        
        # instantiate blood vessels detector
        self._blood_root = orig_path
        self._blood = ExtractBloodVessels(self._blood_root, im_file, masks_dir)

        # Labels match the structure of the annotations file:
        '''
        Averages of the pixels values of all images:
        Annotations contain:
        position: meaning 
        0 - 1: drusen/exudates 
        2 - 3: texture 
        4 - 5: Camera effects
        6 - 7: Outside/black
        8 - 9: Optic Disc
        '''

        self._labels =  [Labels.Drusen, Labels.Drusen, Labels.Background, Labels.Background, 
                    Labels.CameraHue, Labels.CameraHue, 
                    Labels.Outside, Labels.Outside, Labels.OD, Labels.OD]

    def display_current(self, prediction, with_camera = False):
        self.display_artifact(prediction, Labels.Drusen, (0, 255, 0), "Drusen")
        self.display_artifact(prediction, Labels.Outside, (0, 0, 255), "Haemorages")

    def _get_predicted_region(self, label):
        region = self._prediction.copy()
        region [region != label] = 0
        return region

    def find_bright_regions(self, thresh = 0.005, abs_area = 50):
        '''
        Finds suspicious bright regions, refines the prediction by:
        - Removing areas of large size: >= image * thresh
        - and small size: <= abs_area
        '''
        assert( 0 < thresh < 1), "Threshold is in (0, 1)"
        if self._prediction.size == 0:
            self._prediction = self.analyze_image()

        # cutoff area
        area = cv2.countNonZero(self._mask) * thresh

        # get bright regions and combine them
        ods = self._get_predicted_region(Labels.OD)
        drusen = self._get_predicted_region(Labels.Drusen)
        combined = ods + drusen
              
        # relabel the combined regions & calculate large removable regions
        # this will remove OD and related effects
        labelled, n = mh.label(combined, Bc = np.ones((9, 9)))
        sizes = mh.labeled.labeled_size(labelled)[1:]
        to_remove = np.argwhere(sizes >= area) + 1        

        #compute small regions to be removed
        to_remove_small = np.argwhere(sizes <= abs_area) + 1
        to_remove = np.r_[to_remove, to_remove_small]

        # remove large areas from prediction matrix
        for i in to_remove:
            self._prediction [labelled == i] = Labels.Masked

        # re-label the rest of the OD artifacts with Drusen
        #self._prediction[self._prediction == Labels.OD] = Labels.Drusen
        self.display_current(self._prediction)

    def mask_off_blood_vessels(self):

        # predictions should exist by now
        assert(self._prediction.size > 0 ), "Prediction labels not computed"

        # get blood
        im_norm = self._blood.preprocess()
        blood_markers = self._blood.extract_blood_vessels(im_norm)

        blood_mask = self._blood.mask

        # need to rescale the mask back to original size
        # the mask gets scaled down during haar transform of ExtractBloodVessels
        blood_markers = ImageReader.rescale_mask(self.image, blood_markers)

        # mask off blood vessels
        self._prediction [blood_markers != 0] = Labels.Masked
        self._mask [blood_markers != 0] = 0