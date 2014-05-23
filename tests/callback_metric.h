/*
 * Copyright 2011-2013 NVIDIA Corporation. All rights reserved
 *
 * Sample app to demonstrate use of CUPTI library to obtain metric values
 * using callbacks for CUDA runtime APIs
 *
*/

#pragma once

#include <stdio.h>
#include <cuda.h>
#include <cupti.h>
#include "testBfs.h"

#define METRIC_NAME_TESLA "branch_efficiency"
#define METRIC_NAME_FERMI "ipc"

#define DRIVER_API_CALL(apiFuncCall)                                           \
do {                                                                           \
    CUresult _status = apiFuncCall;                                            \
    if (_status != CUDA_SUCCESS) {                                             \
        fprintf(stderr, "%s:%d: error: function %s failed with error %d.\n",   \
                __FILE__, __LINE__, #apiFuncCall, _status);                    \
        exit(-1);                                                              \
    }                                                                          \
} while (0)

#define RUNTIME_API_CALL(apiFuncCall)                                          \
do {                                                                           \
    cudaError_t _status = apiFuncCall;                                         \
    if (_status != cudaSuccess) {                                              \
        fprintf(stderr, "%s:%d: error: function %s failed with error %s.\n",   \
                __FILE__, __LINE__, #apiFuncCall, cudaGetErrorString(_status));\
        exit(-1);                                                              \
    }                                                                          \
} while (0)

#define CUPTI_CALL(call)                                                \
  do {                                                                  \
    CUptiResult _status = call;                                         \
    if (_status != CUPTI_SUCCESS) {                                     \
      const char *errstr;                                               \
      cuptiGetResultString(_status, &errstr);                           \
      fprintf(stderr, "%s:%d: error: function %s failed with error %s.\n", \
              __FILE__, __LINE__, #call, errstr);                       \
      exit(-1);                                                         \
    }                                                                   \
  } while (0)

#define ALIGN_SIZE (8)
#define ALIGN_BUFFER(buffer, align)                                            \
  (((uintptr_t) (buffer) & ((align)-1)) ? ((buffer) + (align) - ((uintptr_t) (buffer) & ((align)-1))) : (buffer)) 

// User data for event collection callback
typedef struct MetricData_st {
  // the device where metric is being collected
  CUdevice device;
  // the set of event groups to collect for a pass
  CUpti_EventGroupSet *eventGroups;
  // the current number of events collected in eventIdArray and
  // eventValueArray
  uint32_t eventIdx;
  // the number of entries in eventIdArray and eventValueArray
  uint32_t numEvents;
  // array of event ids
  CUpti_EventID *eventIdArray;
  // array of event values
  uint64_t *eventValueArray;
} MetricData_t;

// Device code
__global__ void VecAdd(const int* A, const int* B, int* C, int N)
{
  int i = blockDim.x * blockIdx.x + threadIdx.x;
  if (i < N)
    C[i] = A[i] + B[i];
}

static void
initVec(int *vec, int n)
{
  for (int i=0; i< n; i++)
    vec[i] = i;
}

void CUPTIAPI
getMetricValueCallback(void *userdata, CUpti_CallbackDomain domain,
                       CUpti_CallbackId cbid, const CUpti_CallbackData *cbInfo)
{
  MetricData_t *metricData = (MetricData_t*)userdata;
  unsigned int i, j, k;

  // This callback is enabled only for launch so we shouldn't see
  // anything else.
  if (cbid != CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020) {
    printf("%s:%d: unexpected cbid %d\n", __FILE__, __LINE__, cbid);
    exit(-1);
  }

  // on entry, enable all the event groups being collected this pass,
  // for metrics we collect for all instances of the event
  if (cbInfo->callbackSite == CUPTI_API_ENTER) {
    cudaDeviceSynchronize();

    CUPTI_CALL(cuptiSetEventCollectionMode(cbInfo->context, 
                                           CUPTI_EVENT_COLLECTION_MODE_KERNEL));
    
    for (i = 0; i < metricData->eventGroups->numEventGroups; i++) {
      uint32_t all = 1;
      CUPTI_CALL(cuptiEventGroupSetAttribute(metricData->eventGroups->eventGroups[i],
                                             CUPTI_EVENT_GROUP_ATTR_PROFILE_ALL_DOMAIN_INSTANCES,
                                             sizeof(all), &all));
      CUPTI_CALL(cuptiEventGroupEnable(metricData->eventGroups->eventGroups[i]));
    }
  }

  // on exit, read and record event values
  if (cbInfo->callbackSite == CUPTI_API_EXIT) {
    cudaDeviceSynchronize();
    
    // for each group, read the event values from the group and record
    // in metricData
    for (i = 0; i < metricData->eventGroups->numEventGroups; i++) {
      CUpti_EventGroup group = metricData->eventGroups->eventGroups[i];
      CUpti_EventDomainID groupDomain;
      uint32_t numEvents, numInstances, numTotalInstances;
      CUpti_EventID *eventIds;
      size_t groupDomainSize = sizeof(groupDomain);
      size_t numEventsSize = sizeof(numEvents);
      size_t numInstancesSize = sizeof(numInstances);
      size_t numTotalInstancesSize = sizeof(numTotalInstances);
      uint64_t *values, normalized, sum;
      size_t valuesSize, eventIdsSize;
      
      CUPTI_CALL(cuptiEventGroupGetAttribute(group, 
                                             CUPTI_EVENT_GROUP_ATTR_EVENT_DOMAIN_ID,
                                             &groupDomainSize, &groupDomain));
      CUPTI_CALL(cuptiDeviceGetEventDomainAttribute(metricData->device, groupDomain, 
                                                    CUPTI_EVENT_DOMAIN_ATTR_TOTAL_INSTANCE_COUNT,
                                                    &numTotalInstancesSize, &numTotalInstances));
      CUPTI_CALL(cuptiEventGroupGetAttribute(group, 
                                             CUPTI_EVENT_GROUP_ATTR_INSTANCE_COUNT,
                                             &numInstancesSize, &numInstances));
      CUPTI_CALL(cuptiEventGroupGetAttribute(group, 
                                             CUPTI_EVENT_GROUP_ATTR_NUM_EVENTS,
                                             &numEventsSize, &numEvents));
      eventIdsSize = numEvents * sizeof(CUpti_EventID);
      eventIds = (CUpti_EventID *)malloc(eventIdsSize);
      CUPTI_CALL(cuptiEventGroupGetAttribute(group, 
                                             CUPTI_EVENT_GROUP_ATTR_EVENTS,
                                             &eventIdsSize, eventIds));

      valuesSize = sizeof(uint64_t) * numInstances;
      values = (uint64_t *)malloc(valuesSize);
      
      for (j = 0; j < numEvents; j++) {
        CUPTI_CALL(cuptiEventGroupReadEvent(group, CUPTI_EVENT_READ_FLAG_NONE, 
                                            eventIds[j], &valuesSize, values));
        if (metricData->eventIdx >= metricData->numEvents) {
          fprintf(stderr, "error: too many events collected, metric expects only %d\n",
                  (int)metricData->numEvents);
          exit(-1);
        }
        
        // sum collect event values from all instances
        sum = 0;
        for (k = 0; k < numInstances; k++)
          sum += values[k];

        // normalize the event value to represent the total number of
        // domain instances on the device
        normalized = (sum * numTotalInstances) / numInstances;

        metricData->eventIdArray[metricData->eventIdx] = eventIds[j];
        metricData->eventValueArray[metricData->eventIdx] = normalized;
        metricData->eventIdx++;

        // print collected value
        {
          char eventName[128];
          size_t eventNameSize = sizeof(eventName) - 1;
          CUPTI_CALL(cuptiEventGetAttribute(eventIds[j], CUPTI_EVENT_ATTR_NAME, 
                                            &eventNameSize, eventName));
          eventName[127] = '\0';
          printf("\t%s = %llu (", eventName, (unsigned long long)sum);
          if (numInstances > 1) {
            for (k = 0; k < numInstances; k++) {
              if (k != 0)
                printf(", ");
              printf("%llu", (unsigned long long)values[k]);
            }
          }

          printf(")\n");
          printf("\t%s (normalized) (%llu * %u) / %u = %llu\n",
                 eventName, (unsigned long long)sum,
                 numTotalInstances, numInstances,
                 (unsigned long long)normalized);
        }
      }

      free(values);
    }

    for (i = 0; i < metricData->eventGroups->numEventGroups; i++)
      CUPTI_CALL(cuptiEventGroupDisable(metricData->eventGroups->eventGroups[i]));
  }
}

static void
runPass(int argc, char **argv)
{
	testBfs(argc, argv);
} 

int 
cuptiProfile(int argc, char *argv[])
{
  CUpti_SubscriberHandle subscriber;
  CUcontext context = 0;
  CUdevice device = 0;
  int computeCapabilityMajor=0;
  int computeCapabilityMinor=0;
  int deviceNum;
  int deviceCount;
  char deviceName[32];
  char *metricName;
  CUpti_MetricID metricId;
  CUpti_EventGroupSets *passData;
  MetricData_t metricData;
  unsigned int pass;
  CUpti_MetricValue metricValue;
  uint64_t duration = 1;

  printf("Usage: %s [device_num] [metric_name]\n", argv[0]);

  // make sure activity is enabled before any CUDA API
  CUPTI_CALL(cuptiActivityEnable(CUPTI_ACTIVITY_KIND_KERNEL));

  DRIVER_API_CALL(cuInit(0));
  DRIVER_API_CALL(cuDeviceGetCount(&deviceCount));
  if (deviceCount == 0) {
    printf("There is no device supporting CUDA.\n");
    return -2;
  }

  if (argc > 1)
    deviceNum = atoi(argv[1]);
  else
    deviceNum = 0;
  printf("CUDA Device Number: %d\n", deviceNum);

  DRIVER_API_CALL(cuDeviceGet(&device, deviceNum));
  DRIVER_API_CALL(cuDeviceGetName(deviceName, 32, device));
  printf("CUDA Device Name: %s\n", deviceName);

  DRIVER_API_CALL(cuDeviceComputeCapability(&computeCapabilityMajor, 
                                            &computeCapabilityMinor, 
                                            device));
  DRIVER_API_CALL(cuCtxCreate(&context, 0, device));

    
  // Get the name of the metric to collect
  if (argc > 2)
    metricName = argv[2];
  else {
    if (computeCapabilityMajor > 1) {
      metricName = METRIC_NAME_FERMI;
    }
    else {    
      metricName = METRIC_NAME_TESLA;
    }
  }

  // need to collect duration of kernel execution without any event
  // collection enabled (some metrics need kernel duration as part of
  // calculation). The only accurate way to do this is by using the
  // activity API.
  {
    CUpti_Activity *record = NULL;
    size_t size = 16 * 1024;
    uint8_t *buffer = (uint8_t *)malloc(size+ALIGN_SIZE);
    size_t validBufferSizeBytes;

    CUPTI_CALL(cuptiActivityEnqueueBuffer(context, 0, ALIGN_BUFFER(buffer, ALIGN_SIZE), size));
    runPass(argc - 3, argv + 3);
    cudaDeviceSynchronize();
    
    CUPTI_CALL(cuptiActivityDequeueBuffer(context, 0, &buffer, &validBufferSizeBytes));

    // launched only one kernel so should have only one record
    CUPTI_CALL(cuptiActivityGetNextRecord(buffer, validBufferSizeBytes, &record));
    CUpti_ActivityKernel2 *kernel = (CUpti_ActivityKernel2 *)record;
    if (kernel->kind != CUPTI_ACTIVITY_KIND_KERNEL) {
      fprintf(stderr, "expected kernel activity record, got %d\n", (int)kernel->kind);
      exit(-1);
    }

    duration = kernel->end - kernel->start;
    printf("Duration = %lluns\n", (unsigned long long)duration);

    free(buffer);
  }

  // setup launch callback for event collection
  CUPTI_CALL(cuptiSubscribe(&subscriber, (CUpti_CallbackFunc)getMetricValueCallback, &metricData));
  CUPTI_CALL(cuptiEnableCallback(1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, 
                                 CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020));
  
  // allocate space to hold all the events needed for the metric
  CUPTI_CALL(cuptiMetricGetIdFromName(device, metricName, &metricId));
  CUPTI_CALL(cuptiMetricGetNumEvents(metricId, &metricData.numEvents));
  metricData.device = device;
  metricData.eventIdArray = (CUpti_EventID *)malloc(metricData.numEvents * sizeof(CUpti_EventID));
  metricData.eventValueArray = (uint64_t *)malloc(metricData.numEvents * sizeof(uint64_t));
  metricData.eventIdx = 0;

  // get the number of passes required to collect all the events
  // needed for the metric and the event groups for each pass
  CUPTI_CALL(cuptiMetricCreateEventGroupSets(context, sizeof(metricId), &metricId, &passData));
  for (pass = 0; pass < passData->numSets; pass++) {
    printf("Pass %u\n", pass);
    metricData.eventGroups = passData->sets + pass;
    runPass(argc - 3, argv + 3);
  }

  if (metricData.eventIdx != metricData.numEvents) {
    fprintf(stderr, "error: expected %u metric events, got %u\n",
            metricData.numEvents, metricData.eventIdx);
    exit(-1);
  }

  // use all the collected events to calculate the metric value
  CUPTI_CALL(cuptiMetricGetValue(device, metricId,
                                 metricData.numEvents * sizeof(CUpti_EventID),
                                 metricData.eventIdArray,
                                 metricData.numEvents * sizeof(uint64_t),
                                 metricData.eventValueArray,
                                 duration, &metricValue));
  
  // print metric value, we format based on the value kind
  {
    CUpti_MetricValueKind valueKind;
    size_t valueKindSize = sizeof(valueKind);
    CUPTI_CALL(cuptiMetricGetAttribute(metricId, CUPTI_METRIC_ATTR_VALUE_KIND, 
                                       &valueKindSize, &valueKind));
    switch (valueKind) {
    case CUPTI_METRIC_VALUE_KIND_DOUBLE:
      printf("Metric %s = %f\n", metricName, metricValue.metricValueDouble);
      break;
    case CUPTI_METRIC_VALUE_KIND_UINT64:
      printf("Metric %s = %llu\n", metricName,
             (unsigned long long)metricValue.metricValueUint64);
      break;
    case CUPTI_METRIC_VALUE_KIND_INT64:
      printf("Metric %s = %lld\n", metricName,
             (long long)metricValue.metricValueInt64);
      break;
    case CUPTI_METRIC_VALUE_KIND_PERCENT:
      printf("Metric %s = %f%%\n", metricName, metricValue.metricValuePercent);
      break;
    case CUPTI_METRIC_VALUE_KIND_THROUGHPUT:
      printf("Metric %s = %llu bytes/sec\n", metricName, 
             (unsigned long long)metricValue.metricValueThroughput);
      break;
    case CUPTI_METRIC_VALUE_KIND_UTILIZATION_LEVEL:
      printf("Metric %s = utilization level %u\n", metricName, 
             (unsigned int)metricValue.metricValueUtilizationLevel);
      break;
    default:
      fprintf(stderr, "error: unknown value kind\n");
      exit(-1);
    }
  }

  CUPTI_CALL(cuptiUnsubscribe(subscriber));
  return 0;
}
