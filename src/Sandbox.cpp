#include <opencv2/opencv.hpp>
#include <iostream>

#include <CL/cl.h>
#include "SystechDefinitions.h"

// ----------------------------------------------------------------------------- General Configuration

#define DebugLevel 				0x0E	// 0x1110b

// ---------------------------------------------------------------------- End of General Configuration

// --------------------------------------------------------------------------- Preprocessor Directives

// Debug Functions
#define OpenCLFault() cleanUpOpenCL(SandboxCLContext, MaliCommandQueue, NebulaProgram, \
		NebulaKernel, FrameImages, NumFrameImages)

#define CatchCLFault(cond, fmt, args...) if (cond) {OpenCLFault(); \
	DEBUG_ERROR("[OpenCL Fault] " fmt, ## args); return -1;}

#define CatchGeneralFault(cond, fmt, args...) if (cond) {DEBUG_ERROR(fmt, ## args); return -1;}
// ------------------------------------------------------------------- End of  Preprocessor Directives

#include "OpenCL-Common/OpenCL-Common.h"

using namespace cv;
using namespace std;

int main(int argc, char** argv){
	DEBUG_INFO("Program Entry Point, Compiled on: %s:%s", __DATE__, __TIME__);
	// ------------------------------------------------------------------------- OpenCV Initialization
	DEBUG_INFO("Starting OpenCV Initialization");
	VideoCapture VideoStream;
	switch (argc){
	case 1:
		VideoStream.open(0);
		if (!VideoStream.isOpened()){
			cout << "Couldn't Open Capture Device" << endl;
			return -1;
		}
		cout << "Opened Capture Device Number 0" << endl;
		break;
	case 2:
		VideoStream.open(argv[1]);
		if (!VideoStream.isOpened()){
			cout << "Couldn't Open Video Stream" << endl;
			return -1;
		}
		cout << "Opened Video Stream " << '"' << argv[1] << '"' << endl;
		break;
	default:
		//TODO: Implement other arguments handler.
		break;
	}

	const char Window_InVideo[] = "Input Video Stream Viewer";
	const char Window_OutVideo[] = "Output Stream Viewer";

	namedWindow(Window_InVideo, CV_WINDOW_KEEPRATIO);
	namedWindow(Window_OutVideo, CV_WINDOW_KEEPRATIO);

	Mat Frame;
	VideoStream.read(Frame);
	Mat OutputFrame(Frame.rows, Frame.cols, CV_8UC1, NULL, Frame.step/3);
	DEBUG_INFO("End of OpenCV Initialization");
	// --------------------------------------------------------------------- End OpenCV Initialization

	// ------------------------------------------------------------------------- OpenCL Initialization
	DEBUG_INFO("OpenCL Initialization");
	cl_context SandboxCLContext = 0;
	cl_command_queue MaliCommandQueue = 0;
	cl_program NebulaProgram = 0;
	cl_program BGRtoGrayProgram = 0;
	cl_device_id DeviceID = 0;
	cl_kernel BGRtoGrayKernel = 0;
	cl_kernel NebulaKernel = 0;
	cl_mem FrameImages[3] = {0,0,0};				// InFrame, GrayFrame, OutFrame
	char NumFrameImages = 3;
	cl_int errorCode;

	CatchCLFault(!createContext(&SandboxCLContext), "Failed to create an OpenCL Context!")
	CatchCLFault(!createCommandQueue(SandboxCLContext, &MaliCommandQueue, &DeviceID), \
			"Failed to create the OpenCL Command Queue")
	CatchCLFault(!createProgram(SandboxCLContext, DeviceID, "../src/Nebula.cl", &NebulaProgram), \
			"Failed to create OpenCL \"Nebula\" program")
	CatchCLFault(!createProgram(SandboxCLContext, DeviceID, "../src/BGRtoGray.cl", &BGRtoGrayProgram), \
			"Failed to create OpenCL \"BGR2Gray\" program")

	NebulaKernel = clCreateKernel(NebulaProgram, "NebulaKernel", &errorCode);
    CatchCLFault(!checkSuccess(errorCode), "Failed to create OpenCL kernel!")

	BGRtoGrayKernel = clCreateKernel(BGRtoGrayProgram, "BGRtoGrayKernel", &errorCode);
    CatchCLFault(!checkSuccess(errorCode), "Failed to create OpenCL kernel!")

    // TODO: Fix Nebula Events Name Conventions
    typedef struct S_SandboxCLEvents{
		cl_event SandboxCLEventsArray[6];

    	cl_event& MapInFrame()		{return SandboxCLEventsArray[0];}
    	cl_event& UnMapInFrame()	{return SandboxCLEventsArray[1];}
    	cl_event& BGRtoGrayKernel()	{return SandboxCLEventsArray[2];}
    	cl_event& NebulaKernel()	{return SandboxCLEventsArray[3];}
    	cl_event& MapOutFrame()		{return SandboxCLEventsArray[4];}
    	cl_event& UnMapOutFrame()	{return SandboxCLEventsArray[5];}

    	cl_event  operator [] (unsigned i) const {return this->SandboxCLEventsArray[i];}
    	cl_event& operator [] (unsigned i)		 {return this->SandboxCLEventsArray[i];}
//    	operator** cl_event() const {return this->SandboxCLEventsArray;}
    } T_SandboxCLEvents;

    T_SandboxCLEvents SandboxCLEvents;

    uchar* CL_InFrame;
    uchar* CL_OutFrame;
    cl_image_format CL_GrayFrameFormat;
    CL_GrayFrameFormat.image_channel_order = CL_RGBA;
    CL_GrayFrameFormat.image_channel_data_type = CL_FLOAT;

    size_t RGBFrameSize 	 = 3*sizeof(unsigned char)*Frame.cols*Frame.rows;
    size_t GrayFrameSize 	 = 1*sizeof(unsigned char)*Frame.cols*Frame.rows;
	size_t RGBImageRowPitch  = 3*sizeof(unsigned char)*Frame.cols;
	size_t GrayImageRowPitch = 1*sizeof(unsigned char)*Frame.cols;

    struct S_ImageFrameDescriptor{
    	size_t ImageOrigin[3];
    	size_t ImageRegion[3];
    	size_t ImageRowPitch;
    	size_t ImageSlicePitch;
    }ImageFrameDescriptor;

    ImageFrameDescriptor.ImageOrigin[0] = 0;
    ImageFrameDescriptor.ImageOrigin[1] = 0;
    ImageFrameDescriptor.ImageOrigin[2] = 0;
    ImageFrameDescriptor.ImageRegion[0] = Frame.cols;
    ImageFrameDescriptor.ImageRegion[1] = Frame.rows;
    ImageFrameDescriptor.ImageRegion[2] = 1;
    ImageFrameDescriptor.ImageRowPitch = 4*Frame.cols * sizeof(uchar);
    ImageFrameDescriptor.ImageSlicePitch = 0;

    size_t globalWorkSize[3];
    globalWorkSize[0] = Frame.cols;
    globalWorkSize[1] = Frame.rows;
    globalWorkSize[2] = 1;

    DEBUG_INFO("End of OpenCL Initialization");
	// --------------------------------------------------------------------- End OpenCL Initialization

    // ----------------------------------------------------------------------------- Nebula Constrains
    DEBUG_INFO("Setting up Nebula Constrains");

    DEBUG_INFO("Fetching Guide Frame");
	VideoStream.read(Frame);
	unsigned int NebulaAmplificator = 10;
	bool CLOpsSuccess = true;

	DEBUG_INFO("Creating OpenCL Image Objects");
	FrameImages[0] = clCreateBuffer(SandboxCLContext, CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR, \
			RGBFrameSize, NULL, &errorCode);
	CLOpsSuccess &= checkSuccess(errorCode);

	FrameImages[1] = clCreateImage2D(SandboxCLContext, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, \
			&CL_GrayFrameFormat, Frame.cols, Frame.rows, 0, NULL, &errorCode);	// GrayFrame
	CLOpsSuccess  &= checkSuccess(errorCode);

	FrameImages[2] = clCreateBuffer(SandboxCLContext, CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR, \
			GrayFrameSize, NULL, &errorCode);
	CLOpsSuccess  &= checkSuccess(errorCode);

	CatchCLFault(!CLOpsSuccess, "Failed to create OpenCL Image/Buffer Objects")
	DEBUG_INFO("Successfully Created Image and Buffer Objects");

	// ---------------------------------------------------------------------- End of Nebula Constrains
	// TODO: [Important] Redefine Process Lifetime
	while (waitKey(1)!=27){
		// TODO: [Important] Improve Event Handling

		DEBUG_INFO("Enqueuing OpenCL Map InFrame Image Buffer");
		CL_InFrame = (uchar*)clEnqueueMapBuffer(MaliCommandQueue, FrameImages[0], CL_FALSE, CL_MAP_WRITE, \
				0, RGBFrameSize, 0, NULL, &SandboxCLEvents.MapInFrame(), &errorCode);
		CLOpsSuccess &= errorCode;
		CatchCLFault(!checkSuccess(CLOpsSuccess), "Failed to Map OpenCL InFrame Image Buffer.")
		DEBUG_INFO("OpenCL InFrame Image Buffer Successfully Mapped!");
		Frame.data = CL_InFrame;							// Pointing to OpenCL Allocated Memory
		clWaitForEvents(1, &SandboxCLEvents.MapInFrame());	// Wait until GPU Mem is Mapped on Host
		VideoStream.read(Frame);							// Get Frame

		CatchCLFault(clEnqueueUnmapMemObject(MaliCommandQueue, FrameImages[0], CL_InFrame, 0, NULL, \
				&SandboxCLEvents.UnMapInFrame()), "Error While Unmapping InFrame Buffer Memory!")
		DEBUG_INFO("OpenCL InFrame Image Buffer Successfully UnMapped!");


		DEBUG_INFO("Setting BGR2Gray OpenCL Kernel Arguments");
		CLOpsSuccess &= checkSuccess(clSetKernelArg(BGRtoGrayKernel, 0, sizeof(cl_mem), &FrameImages[0]));
		CLOpsSuccess &= checkSuccess(clSetKernelArg(BGRtoGrayKernel, 1, sizeof(cl_mem), &FrameImages[1]));
		CLOpsSuccess &= checkSuccess(clSetKernelArg(BGRtoGrayKernel, 2, sizeof(size_t), &RGBImageRowPitch));
		CatchCLFault(!checkSuccess(CLOpsSuccess), "Failed Setting BGR2Gray OpenCL Kernel Arguments")
		DEBUG_INFO("Success Setting BGR2Gray OpenCL Kernel Arguments");

		DEBUG_INFO("Enqueueing BGR2Gray OpenCL Kernel");
		CatchCLFault(!checkSuccess(clEnqueueNDRangeKernel(MaliCommandQueue, BGRtoGrayKernel, 3, NULL, \
				globalWorkSize, NULL, 0, NULL, &SandboxCLEvents.BGRtoGrayKernel())), "Could Not Enqueue" \
				"BGRtoGray Kernel!")
		CatchCLFault(!checkSuccess(clFinish(MaliCommandQueue)), "Failure while waiting for Command" \
				"Queue to Finish")
		DEBUG_INFO("Finished OpenCL Command Queue!");


		DEBUG_INFO("Setting Nebula OpenCL Kernel Arguments");
		CLOpsSuccess &= checkSuccess(clSetKernelArg(NebulaKernel, 0, sizeof(cl_mem), &FrameImages[1]));
		CLOpsSuccess &= checkSuccess(clSetKernelArg(NebulaKernel, 1, sizeof(cl_mem), &FrameImages[2]));
		CLOpsSuccess &= checkSuccess(clSetKernelArg(NebulaKernel, 2, sizeof(size_t), &GrayImageRowPitch));
		CLOpsSuccess &= checkSuccess(clSetKernelArg(NebulaKernel, 3, sizeof(size_t), &NebulaAmplificator));
		CatchCLFault(!checkSuccess(CLOpsSuccess), "Failed Setting Nebula OpenCL Kernel Arguments")
		DEBUG_INFO("Success Setting Nebula OpenCL Kernel Arguments");

		DEBUG_INFO("Enqueueing Nebula OpenCL Kernel");
		CatchCLFault(!checkSuccess(clEnqueueNDRangeKernel(MaliCommandQueue, NebulaKernel, 3, NULL, \
				globalWorkSize, NULL, 0, NULL, &SandboxCLEvents.NebulaKernel())), "Could Not Enqueue" \
				"NebulaKernel!")
		CatchCLFault(!checkSuccess(clFinish(MaliCommandQueue)), "Failure while waiting for Command" \
				"Queue to Finish")
		DEBUG_INFO("Finished OpenCL Command Queue!");


		DEBUG_INFO("Enqueuing OpenCL Map OutFrame Image Buffer");
		CL_OutFrame = (uchar*)clEnqueueMapBuffer(MaliCommandQueue, FrameImages[2], CL_FALSE, \
				CL_MAP_READ, 0, GrayFrameSize, 0, NULL, &SandboxCLEvents.MapOutFrame(), &errorCode);
		CLOpsSuccess &= errorCode;
		CatchCLFault(!checkSuccess(CLOpsSuccess), "Failed to Map OpenCL OutFrame Image Buffer.")
		DEBUG_INFO("OpenCL OutFrame Image Buffer Successfully Mapped!");

		clWaitForEvents(1, &SandboxCLEvents.MapOutFrame());
		OutputFrame.data = CL_OutFrame;

		imshow(Window_InVideo, Frame);
		imshow(Window_OutVideo, OutputFrame);

		CatchCLFault(clEnqueueUnmapMemObject(MaliCommandQueue, FrameImages[2], CL_OutFrame, 0, \
				NULL, &SandboxCLEvents.UnMapOutFrame()), "Error While Unmapping OutFrame Memory!")
		DEBUG_INFO("OpenCL OutFrame Image Buffer Successfully UnMapped!");

	}
	DEBUG_INFO("Printing Last Events Profiling Info\n");

	DEBUG_INFO("Printing Map InFrame Profiling Info:");
	printProfilingInfo(SandboxCLEvents.MapInFrame());
	DEBUG_INFO("Printing UnMap InFrame Profiling Info:");
	printProfilingInfo(SandboxCLEvents.UnMapInFrame());
	DEBUG_INFO("Printing BGR2Gray Kernel Profiling Info:");
	printProfilingInfo(SandboxCLEvents.BGRtoGrayKernel());
	DEBUG_INFO("Printing Nebula Kernel Profiling Info:");
	printProfilingInfo(SandboxCLEvents.NebulaKernel());
	DEBUG_INFO("Printing Map OutFrame Profiling Info:");
	printProfilingInfo(SandboxCLEvents.MapOutFrame());
	DEBUG_INFO("Printing UnMap OutFrame Profiling Info:");
	clWaitForEvents(1, &SandboxCLEvents.UnMapOutFrame());
	printProfilingInfo(SandboxCLEvents.UnMapOutFrame());
	DEBUG_INFO("End of Profiling Information\n");

	DEBUG_INFO("Releasing Event Objects...");
	CatchCLFault(!checkSuccess(clReleaseEvent(SandboxCLEvents.MapInFrame())), \
			"Could not Release Nebula Kernel Run Event")
	CatchCLFault(!checkSuccess(clReleaseEvent(SandboxCLEvents.UnMapInFrame())), \
			"Could not Release Nebula Kernel Run Event")
	CatchCLFault(!checkSuccess(clReleaseEvent(SandboxCLEvents.BGRtoGrayKernel())), \
			"Could not Release Nebula Kernel Run Event")
	CatchCLFault(!checkSuccess(clReleaseEvent(SandboxCLEvents.NebulaKernel())), \
			"Could not Release Nebula Kernel Run Event")
	CatchCLFault(!checkSuccess(clReleaseEvent(SandboxCLEvents.MapOutFrame())), \
			"Could not Release Nebula Kernel Run Event")
	CatchCLFault(!checkSuccess(clReleaseEvent(SandboxCLEvents.UnMapOutFrame())), \
			"Could not Release Nebula Kernel Run Event")
	DEBUG_INFO("Event Objects Released");

	DEBUG_INFO("\n\n Goodbye my dear and beloved world...\n\n");
	cleanUpOpenCL(SandboxCLContext, MaliCommandQueue, NebulaProgram, NebulaKernel, FrameImages, \
			NumFrameImages);
	destroyAllWindows();
	VideoStream.release();
	return 0;
}
