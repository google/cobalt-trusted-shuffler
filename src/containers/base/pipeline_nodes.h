// Copyright 2026 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_CONTAINERS_BASE_PIPELINE_NODES_H_
#define SRC_CONTAINERS_BASE_PIPELINE_NODES_H_

namespace cobalt::trusted_shuffler {

// A data node of the pipeline: one stream of records flowing between
// containers.
//
// The pipeline's access policy is a graph of these nodes. Each transform in the
// policy lists the nodes it reads and the nodes it writes, and KMS hands a
// container one re-encryption key per node it writes.
//
// Each enumerator's value is that node's id in the access policy. The ids are
// not contiguous here: only nodes a container encrypts data to are listed, so
// the raw client uploads the pipeline reads from Spanner (node 0) are absent,
// and so is node 4, which no transform reads or writes.
//
// Nodes are named after the records they carry rather than after the container
// that reads them, because several containers may read the same node.
enum class Node {
  // Not a data node. Declares an output stream that has no destination, which
  // is every unencrypted stream.
  kNone = -1,

  // The SignedRunContext fixing the parameters of a single run, written by
  // run_context_initializer and read by every stage that validates the run.
  kRunContext = 1,

  // Client messages, re-encrypted one per record and keyed by metric and epoch,
  // written by ingest_uploads.
  kIngestedMessages = 2,

  // One PartialIngestionReport per ingest_uploads worker, carrying that
  // worker's fingerprint and device sketches for ingestion_verification to
  // aggregate.
  kPartialIngestionReports = 3,

  // The IngestionVerificationPayload holding the threshold report and the
  // canonical run fingerprint, written by ingestion_verification.
  kVerifiedIngestion = 5,

  // Client messages surviving the threshold filter, written by
  // threshold_filter.
  kFilteredMessages = 6,

  // Client messages forwarded after counting, written by message_counter.
  kCountedMessages = 7,

  // Dummy padding messages rounding each approved partition up to a multiple of
  // the target batch size, written by pre_padding.
  kPaddingMessages = 8,

  // Client messages assigned to a shuffle group, written by scatter_shuffle.
  kScatteredMessages = 9,

  // TODO(b/563428525): Renumber these to their real node ids when their
  // containers are added to the access policy.
  //
  // The nodes below belong to containers that are not yet part of the access
  // policy, so they have no node id yet. They are numbered from 100 so that
  // they cannot be mistaken for real node ids.

  // One PartialReleaseReport per encrypt_output worker, carrying that worker's
  // output encryption key.
  kPartialReleaseReports = 100,
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_BASE_PIPELINE_NODES_H_
