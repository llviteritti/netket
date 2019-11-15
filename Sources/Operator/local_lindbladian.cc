//
// Created by Filippo Vicentini on 08/11/2019.
//

#include "local_lindbladian.hpp"
#include "abstract_operator.hpp"

namespace netket {

LocalLindbladian::LocalLindbladian(const LocalOperator &H)
    : AbstractOperator(std::make_shared<DoubledHilbert>(H.GetHilbertShared())),
      Hnh_(H),
      H_(H),
      Hnh_dag_(H) {
  Init();
}

void LocalLindbladian::Init() {
  auto j = std::complex<double>(0.0, 1.0);

  Hnh_ = H_;
  for (auto &L : jump_ops_) {
    Hnh_ += ((-0.5 * j) * L.Conjugate().Transpose() * L);
  }
  Hnh_dag_ = Hnh_.Conjugate().Transpose();
}

const std::vector<LocalOperator> &LocalLindbladian::GetJumpOperators() const {
  return jump_ops_;
}

void LocalLindbladian::AddJumpOperator(const netket::LocalOperator &op) {
  jump_ops_.push_back(op);

  Init();
}

void LocalLindbladian::ForEachConn(
    netket::LocalLindbladian::VectorConstRefType v,
    netket::AbstractOperator::ConnCallback callback) const {
  auto N = GetHilbertDoubled().SizePhysical();

  ForEachConnSuperOp(v.head(N), v.tail(N), [&](ConnectorSuperopRef conn) {
    auto tochange =
        SiteType(conn.tochange_row.size() + conn.tochange_col.size());
    std::copy(conn.tochange_row.begin(), conn.tochange_row.end(),
              tochange.begin());
    std::transform(conn.tochange_col.begin(), conn.tochange_col.end(),
                   tochange.begin() + conn.tochange_row.size(),
                   bind2nd(std::plus<int>(), N));

    auto newconf = std::vector<double>(conn.tochange_row.size() +
                                       conn.tochange_col.size());
    std::copy(conn.newconf_row.begin(), conn.newconf_row.end(),
              newconf.begin());
    std::copy(conn.newconf_col.begin(), conn.newconf_col.end(),
              newconf.begin() + conn.newconf_row.size());

    callback(ConnectorRef{conn.mel, tochange, newconf});
  });
}

void LocalLindbladian::FindConn(VectorConstRefType v, MelType &mel,
                                ConnectorsType &connectors,
                                NewconfsType &newconfs) const {
  connectors.clear();
  newconfs.clear();
  mel.clear();

  auto N = GetHilbertDoubled().SizePhysical();
  auto vrow = v.head(N);
  auto vcol = v.tail(N);

  ForEachConnSuperOp(vrow, vcol, [&](ConnectorSuperopRef conn) {
    auto tochange =
        SiteType(conn.tochange_row.size() + conn.tochange_col.size());
    std::copy(conn.tochange_row.begin(), conn.tochange_row.end(),
              tochange.begin());
    std::transform(conn.tochange_col.begin(), conn.tochange_col.end(),
                   tochange.begin() + conn.tochange_row.size(),
                   bind2nd(std::plus<int>(), N));

    auto newconf = std::vector<double>(conn.tochange_row.size() +
                                       conn.tochange_col.size());
    std::copy(conn.newconf_row.begin(), conn.newconf_row.end(),
              newconf.begin());
    std::copy(conn.newconf_col.begin(), conn.newconf_col.end(),
              newconf.begin() + conn.newconf_row.size());

    mel.push_back(conn.mel);
    connectors.emplace_back(std::move(tochange));
    newconfs.emplace_back(std::move(newconf));
  });
}

void LocalLindbladian::ForEachConnSuperOp(
    netket::LocalLindbladian::VectorConstRefType vrow,
    netket::LocalLindbladian::VectorConstRefType vcol,
    netket::LocalLindbladian::ConnSuperOpCallback callback) const {
  auto im = Complex(0.0, 1.0);

  // The logic behind the use of Hnh_dag_ and Hnh_ is taken from reference
  // arXiv:1504.05266

  // Compute term Hnh \kron Identity
  // Find connections <vrow|Hnh|x>
  Hnh_dag_.ForEachConn(vrow, [&](ConnectorRef conn) {
    callback(ConnectorSuperopRef{
        im * conn.mel, conn.tochange, conn.newconf, {}, {}});
  });

  // Compute term Identity \kron Hnh^\dagger
  // Find connections <vcol|Hnh^\dag|x>
  Hnh_.ForEachConn(vcol, [&](ConnectorRef conn) {
    callback(ConnectorSuperopRef{
        -im * conn.mel, {}, {}, conn.tochange, conn.newconf});
  });

  // Compute term \sum_i L_i \otimes L_i^\dagger
  // Iterate over all jump operators
  for (auto &op : jump_ops_) {
    op.ForEachConn(vrow, [&](ConnectorRef conn_row) {
      op.ForEachConn(vcol, [&](ConnectorRef conn_col) {
        callback(ConnectorSuperopRef{std::conj(conn_row.mel) * conn_col.mel,
                                     conn_row.tochange, conn_row.newconf,
                                     conn_col.tochange, conn_col.newconf});
      });
    });
  }
}

const DoubledHilbert &LocalLindbladian::GetHilbertDoubled() const {
  return *GetHilbertDoubledShared();
}

std::shared_ptr<const DoubledHilbert>
LocalLindbladian::GetHilbertDoubledShared() const {
  return std::static_pointer_cast<const DoubledHilbert>(GetHilbertShared());
}

}  // namespace netket