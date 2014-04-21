#pragma once

#include <dragon_li/util/userConfig.h>
#include <dragon_li/util/debug.h>
#include <dragon_li/util/random.h>
#include <dragon_li/util/memsetDevice.h>
#include <dragon_li/util/ctaOutputAssignment.h>

namespace dragon_li {
namespace amr {

template < typename Settings >
class AmrBase {

public:
	typedef typename Settings::Types Types;
	typedef typename Settings::SizeType SizeType;
	typedef typename Settings::DataType DataType;
	typedef typename dragon_li::util::CtaOutputAssignment<SizeType> CtaOutputAssignment;

	class UserConfig : public dragon_li::util::UserConfig {
	public:
	
		SizeType maxGridDataSize;
		SizeType maxRefineLevel;
	
		UserConfig(
			bool _verbose,
			bool _veryVerbose,
			SizeType _maxGridDataSize,
			DataType _maxRefineLevel) :
				dragon_li::util::UserConfig(_verbose, _veryVerbose),
				maxGridDataSize(_maxGridDataSize),
				maxRefineLevel(_maxRefineLevel) {}
	};

	//User config
	bool verbose;
	bool veryVerbose;

	//AMR Data
	SizeType maxGridDataSize;
	SizeType maxRefineLevel;
	SizeType activeGridSize;

	DataType * devGridData;
	SizeType * devGridPointer;

	//Cta Output Assignement
	CtaOutputAssignment ctaOutputAssignment;

	AmrBase() :
			verbose(false),
			veryVerbose(false),
			maxGridDataSize(0),
			maxRefineLevel(0),
			activeGridSize(0),
			devGridData(NULL),
			devGridPointer(NULL) {}

	virtual int refine() = 0;

	virtual int setup(UserConfig & userConfig) {

		verbose = userConfig.verbose;
		veryVerbose = userConfig.veryVerbose;
		maxGridDataSize = userConfig.maxGridDataSize;
		maxRefineLevel = userConfig.maxRefineLevel;

		cudaError_t retVal;
		if(retVal = cudaMalloc(&devGridData, maxGridDataSize * sizeof(DataType))) {
			errorCuda(retVal);
			return -1;
		}

		DataType startGridValue;
		dragon_li::util::random<DataType>(&startGridValue, 1, 0, Settings::MAX_GRID_VALUE);
		if(dragon_li::util::memsetDevice<Settings::CTAS, Settings::THREADS, DataType, SizeType>
			(devGridData, startGridValue, 1))
			return -1;

		activeGridSize = 1;

		if(retVal = cudaMalloc(&devGridPointer, maxGridDataSize * sizeof(SizeType))) {
			errorCuda(retVal);
			return -1;
		}
		if(dragon_li::util::memsetDevice<Settings::CTAS, Settings::THREADS, SizeType, SizeType>
			(devGridPointer, -1, maxGridDataSize))
			return -1;

		if(ctaOutputAssignment.setup() != 0)
			return -1;

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
}
