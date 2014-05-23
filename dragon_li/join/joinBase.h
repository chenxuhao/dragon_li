#pragma once

#undef REPORT_BASE
#define REPORT_BASE

namespace dragon_li {
namespace join {

template< typename Settings >
class JoinBase {

public:

	typedef typename Settings::Types Types;
	typedef typename Settings::SizeType SizeType;
	typedef typename Settings::DataType DataType;


	//User control
	bool verbose;
	bool veryVerbose;

	//Join information
	SizeType inputCountLeft;
	SizeType inputCountRight;
	SizeType outputCount;

	//Join Device information
	DataType * devJoinInputLeft;
	DataType * devJoinInputRight;
	DataType * devJoinOutput;
	SizeType * devJoinOutputCount;

	JoinBase() : 
		verbose(false),
		veryVerbose(false),
		inputCountLeft(0),
		inputCountRight(0),
		outputCount(0),
		devjoinInputLeft(NULL),
		devJoinInputRight(NULL),
		devJoinOutput(NULL),
		devJoinOutputCount(NULL) {}

	virtual int join() = 0;

	virtual int setup(JoinData<Types> joinData) {

		inputCountLeft = joinData.inputCountLeft;
		inputCountRight = joinData.inputCountRight;

		cudaError_t retVal;

		if(retVal = cudaMalloc(&devJoininputLeft, inputCountLeft * sizeof(DataType))) {
			errorCuda(retVal);
			return -1;
		}
		if(retVal = cudaMemcpy(devJoinInputLeft, 
								joinData.inputLeft, 
								inputCountLeft * sizeof(DataType),
								cudaMemcpyHostToDevice))
			return -1;
		}
		if(retVal = cudaMalloc(&devJoininputRight, inputCountRight * sizeof(DataType))) {
			errorCuda(retVal);
			return -1;
		}
		if(retVal = cudaMemcpy(devJoinInputRight, 
								joinData.inputRight, 
								inputCountRight * sizeof(DataType),
								cudaMemcpyHostToDevice))
			return -1;
		}

		return 0;
	}

	virtual int displayResult() {
		return 0;
	}

	virtual int finish() {
		return 0;
	}
};

}