#include <opencv.hpp>
#include <stdio.h>

using namespace cv;
using namespace std;

int main(int argc, char** argv){
	if (argc<2){
		cout<<"Please Specify an Image as An Argument while calling this program"<<endl;
		return 1;
	}
	Mat Image=imread(argv[1],-1);
	if (Image.empty()) return -1;
	namedWindow("Picture", WINDOW_NORMAL);
	imshow("Picture", Image);
	waitKey(0);
	destroyAllWindows();
}
