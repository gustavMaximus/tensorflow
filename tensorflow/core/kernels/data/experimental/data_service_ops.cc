/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/kernels/data/experimental/data_service_ops.h"

#include "tensorflow/core/data/service/data_service.h"
#include "tensorflow/core/framework/dataset.h"
#include "tensorflow/core/kernels/data/dataset_utils.h"
#include "tensorflow/core/platform/errors.h"

namespace tensorflow {
namespace data {
namespace {
Status ParseProcessingMode(const tstring& s, ProcessingMode* mode) {
  if (s == "parallel_epochs") {
    *mode = ProcessingMode::PARALLEL_EPOCHS;
  } else if (s == "one_epoch") {
    *mode = ProcessingMode::ONE_EPOCH;
  } else {
    return errors::InvalidArgument("Unrecognized processing mode: ", s);
  }
  return Status::OK();
}
}  // namespace

RegisterDatasetOp::RegisterDatasetOp(OpKernelConstruction* ctx)
    : OpKernel(ctx) {
  int64 external_state_policy_int;
  OP_REQUIRES_OK(
      ctx, ctx->GetAttr(kExternalStatePolicy, &external_state_policy_int));
  external_state_policy_ =
      SerializationContext::ExternalStatePolicy(external_state_policy_int);
}

void RegisterDatasetOp::Compute(OpKernelContext* ctx) {
  DatasetBase* dataset;
  OP_REQUIRES_OK(ctx, GetDatasetFromVariantTensor(ctx->input(0), &dataset));

  tstring address;
  OP_REQUIRES_OK(ctx, ParseScalarArgument(ctx, kAddress, &address));
  OP_REQUIRES(ctx, !address.empty(),
              errors::InvalidArgument(kAddress, " must be non-empty."));

  tstring protocol;
  OP_REQUIRES_OK(ctx, ParseScalarArgument(ctx, kProtocol, &protocol));
  OP_REQUIRES(ctx, !protocol.empty(),
              errors::InvalidArgument(kProtocol, " must be non-empty."));

  SerializationContext::Params params;
  params.external_state_policy = external_state_policy_;
  SerializationContext serialization_ctx(params);
  GraphDef graph_def;
  OP_REQUIRES_OK(
      ctx, AsGraphDef(ctx, dataset, std::move(serialization_ctx), &graph_def));

  DataServiceMasterClient client(address, protocol);
  int64 dataset_id;
  OP_REQUIRES_OK(ctx, client.RegisterDataset(graph_def, &dataset_id));

  Tensor* output;
  OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape{}, &output));
  auto output_dataset_id = output->tensor<int64, 0>();
  output_dataset_id() = dataset_id;
}

CreateJobOp::CreateJobOp(OpKernelConstruction* ctx) : OpKernel(ctx) {}

void CreateJobOp::Compute(OpKernelContext* ctx) {
  int64 dataset_id;
  OP_REQUIRES_OK(ctx, ParseScalarArgument(ctx, kDatasetId, &dataset_id));

  tstring address;
  OP_REQUIRES_OK(ctx, ParseScalarArgument(ctx, kAddress, &address));
  OP_REQUIRES(ctx, !address.empty(),
              errors::InvalidArgument(kAddress, " must be non-empty."));

  tstring protocol;
  OP_REQUIRES_OK(ctx, ParseScalarArgument(ctx, kProtocol, &protocol));
  OP_REQUIRES(ctx, !protocol.empty(),
              errors::InvalidArgument(kProtocol, " must be non-empty."));

  tstring processing_mode_str;
  OP_REQUIRES_OK(
      ctx, ParseScalarArgument(ctx, kProcessingMode, &processing_mode_str));
  ProcessingMode processing_mode;
  OP_REQUIRES_OK(ctx,
                 ParseProcessingMode(processing_mode_str, &processing_mode));

  DataServiceMasterClient client(address, protocol);
  int64 job_id;
  OP_REQUIRES_OK(ctx, client.CreateJob(dataset_id, processing_mode, &job_id));

  JobToken token(job_id);
  Tensor* output;
  OP_REQUIRES_OK(ctx, ctx->allocate_output(0, TensorShape{}, &output));
  auto output_token = output->tensor<Variant, 0>();
  output_token() = token;
}

Status MakeDataServiceIteratorOp::DoCompute(OpKernelContext* ctx) {
  DatasetBase* dataset;
  TF_RETURN_IF_ERROR(GetDatasetFromVariantTensor(ctx->input(0), &dataset));

  const Tensor* token_tensor;
  TF_RETURN_IF_ERROR(ctx->input(kJobToken, &token_tensor));
  JobToken token = *token_tensor->scalar<Variant>()().get<JobToken>();

  IteratorResource* iterator_resource;
  TF_RETURN_IF_ERROR(
      LookupResource(ctx, HandleFromInput(ctx, 2), &iterator_resource));

  core::ScopedUnref unref_iterator(iterator_resource);

  return iterator_resource->SetIteratorFromDataset(ctx, dataset, token);
}

REGISTER_KERNEL_BUILDER(Name("RegisterDataset").Device(DEVICE_CPU),
                        RegisterDatasetOp);
REGISTER_KERNEL_BUILDER(Name("CreateJob").Device(DEVICE_CPU), CreateJobOp);
REGISTER_KERNEL_BUILDER(Name("MakeDataServiceIterator").Device(DEVICE_CPU),
                        MakeDataServiceIteratorOp);

}  // namespace data
}  // namespace tensorflow
