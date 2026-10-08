#pragma once

#include "occupancy/commands/GridCommand.hpp"

namespace agt_map_studio {

class EraseRectangleCommand : public GridCommand {
public:
  static std::unique_ptr<EraseRectangleCommand> create(
      const RefinementModel &model, GridWorldPoint first, GridWorldPoint second);
  static std::unique_ptr<EraseRectangleCommand> create_cells(
      const RefinementModel &model, std::vector<std::size_t> indices);
  explicit EraseRectangleCommand(RefinementOperation operation)
      : operation_(std::move(operation)) {}

  void redo(RefinementModel &model) override;
  void undo(RefinementModel &model) override;
  const RefinementOperation &operation() const override { return operation_; }

private:
  RefinementOperation operation_;
};

}  // namespace agt_map_studio
