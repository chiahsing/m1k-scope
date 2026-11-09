#include "oscilloscope_panel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <initializer_list>
#include <utility>

#include "common.h"
#include "core/device.h"

namespace m1k_scope::ui {

namespace {

// Columns of the measurement checkboxes, so each channel has a row.
constexpr int kCheckColumns = core::m1k::kValuesPerChannel;

}  // namespace

ViewControls::ViewControls(Orientation orientation, core::Quantity quantity,
                           QWidget* parent)
    : QWidget(parent), orientation_(orientation), quantity_(quantity) {
  scale_ = new QComboBox(this);
  center_ = new SpinBox(this);
  // Apply typed values on Enter or focus out, not on every keystroke.
  center_->setKeyboardTracking(false);
  center_->setToolTip("Value at the center of the display");

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(kWidgetSpacing);
  layout->addWidget(scale_);
  layout->addWidget(center_);

  connect(
      scale_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
      [this](int index) { emit ScaleChanged(orientation_, quantity_, index); });
  connect(center_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          [this] {
            emit CenterChanged(orientation_, quantity_,
                               center_->GetQuantityValue());
          });

  ShowQuantity();
}

void ViewControls::SetQuantity(core::Quantity quantity) {
  if (quantity == quantity_) return;
  quantity_ = quantity;
  ShowQuantity();
}

// Shows the scales of the axis of quantity_, at its default view.
void ViewControls::ShowQuantity() {
  const Axis& axis = GetAxis(quantity_);
  const QSignalBlocker scale_blocker(scale_);
  const QSignalBlocker center_blocker(center_);
  scale_->clear();
  for (int i = 0; i < static_cast<int>(axis.per_division.size()); ++i) {
    scale_->addItem(FormatPerDivision(axis, i));
  }
  center_->SetQuantity(quantity_);
  ShowView(GetDefaultScaleIndex(axis), GetDefaultCenter(axis));
}

void ViewControls::ShowView(Orientation orientation, core::Quantity quantity,
                            int scale_index, double center) {
  if (orientation == orientation_ && quantity == quantity_) {
    ShowView(scale_index, center);
  }
}

// Shows a view of the axis, without emitting the signals.
void ViewControls::ShowView(int scale_index, double center) {
  const QSignalBlocker scale_blocker(scale_);
  const QSignalBlocker center_blocker(center_);
  scale_->setCurrentIndex(scale_index);
  center_->SetQuantityValue(center);
}

XAxisPanel::XAxisPanel(QWidget* parent) : QGroupBox("X axis", parent) {
  form_ = CreateGroupForm(this);

  // Time has no item data.
  combo_source_ = new QComboBox(this);
  combo_source_->addItem("Time");
  AddMeasurementItems(combo_source_);
  form_->addRow("Source:", combo_source_);

  // The scale of time, which stays centered on 0 ms, or of the measurement.
  scale_ = new QStackedWidget(this);
  combo_time_ = new QComboBox(scale_);
  combo_window_ = new QComboBox(this);
  for (int i = 0; i < static_cast<int>(GetTimesPerDivision().size()); ++i) {
    combo_time_->addItem(FormatTimePerDivision(i));
    combo_window_->addItem(FormatTimeWindow(i));
  }
  x_view_ = new ViewControls(Orientation::kHorizontal, core::Quantity::kVoltage,
                             scale_);
  scale_->addWidget(combo_time_);
  scale_->addWidget(x_view_);
  // As tall as a row, not stretched by the group.
  scale_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  form_->addRow("Scale:", scale_);
  form_->addRow("Window:", combo_window_);
  combo_window_->setToolTip("How long a frame is");
  ShowTimeScale(GetDefaultTimeScaleIndex());

  connect(combo_source_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &XAxisPanel::PickSource);
  connect(x_view_, &ViewControls::ScaleChanged, this,
          &XAxisPanel::ScaleChanged);
  connect(x_view_, &ViewControls::CenterChanged, this,
          &XAxisPanel::CenterChanged);
  for (QComboBox* combo : {combo_time_, combo_window_}) {
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &XAxisPanel::TimeScaleChanged);
  }
  PickSource();
}

// Shows the scale of the source picked, and the time window for a
// measurement, then announces the source.
void XAxisPanel::PickSource() {
  const QVariant item = combo_source_->currentData();
  const std::optional<int> m =
      item.isValid() ? std::optional(item.toInt()) : std::nullopt;
  if (m) x_view_->SetQuantity(core::kMeasurements[*m].quantity);
  scale_->setCurrentWidget(m ? static_cast<QWidget*>(x_view_) : combo_time_);
  form_->setRowVisible(combo_window_, m.has_value());
  emit XMeasurementChanged(m);
}

void XAxisPanel::ShowTimeScale(int scale_index) {
  for (QComboBox* combo : {combo_time_, combo_window_}) {
    const QSignalBlocker blocker(combo);
    combo->setCurrentIndex(scale_index);
  }
}

void XAxisPanel::ShowView(Orientation orientation, core::Quantity quantity,
                          int scale_index, double center) {
  x_view_->ShowView(orientation, quantity, scale_index, center);
}

YAxisPanel::YAxisPanel(QWidget* parent) : QGroupBox("Y axis", parent) {
  auto* form = CreateGroupForm(this);

  auto* checks = new QGridLayout();
  checks->setSpacing(kRowSpacing);
  for (int m = 0; m < core::kMeasurementCount; ++m) {
    checks_[m] = new QCheckBox(GetMeasurementName(m), this);
    checks_[m]->setIcon(QIcon(GetSwatch(m)));
    checks_[m]->setChecked(IsShownByDefault(m));
    checks->addWidget(checks_[m], m / kCheckColumns, m % kCheckColumns);
    connect(checks_[m], &QCheckBox::toggled, this, [this, m](bool checked) {
      emit MeasurementVisibilityChanged(m, checked);
    });
  }
  form->addRow(checks);

  for (const core::Quantity quantity : kAxisQuantities) {
    auto* view = new ViewControls(Orientation::kVertical, quantity, this);
    views_[static_cast<int>(quantity)] = view;
    form->addRow(quantity == core::Quantity::kCurrent ? "Current:" : "Voltage:",
                 view);
    connect(view, &ViewControls::ScaleChanged, this, &YAxisPanel::ScaleChanged);
    connect(view, &ViewControls::CenterChanged, this,
            &YAxisPanel::CenterChanged);
  }
}

void YAxisPanel::SetXMeasurement(std::optional<int> m) {
  for (int i = 0; i < core::kMeasurementCount; ++i) {
    checks_[i]->setEnabled(m != i);
  }
}

void YAxisPanel::ShowView(Orientation orientation, core::Quantity quantity,
                          int scale_index, double center) {
  for (ViewControls* view : views_) {
    view->ShowView(orientation, quantity, scale_index, center);
  }
}

TriggerPanel::TriggerPanel(core::Device& device, QWidget* parent)
    : QGroupBox("Trigger", parent), device_(device) {
  form_ = CreateGroupForm(this);

  // Picking one source at a time keeps the trigger to a single measurement.
  combo_source_ = new QComboBox(this);
  combo_source_->addItem("Off", core::kNoTrigger);
  AddMeasurementItems(combo_source_);
  form_->addRow("Source:", combo_source_);

  // Adds a row of radio buttons with the given ids, the first one checked.
  // Sets widget to the row's field and returns the buttons' group.
  auto add_radios =
      [this](const char* label, QWidget*& widget,
             std::initializer_list<std::pair<int, const char*>> radios) {
        widget = new QWidget(this);
        auto* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* group = new QButtonGroup(this);
        for (const auto& [id, name] : radios) {
          auto* radio = new QRadioButton(name, widget);
          group->addButton(radio, id);
          layout->addWidget(radio);
        }
        group->button(radios.begin()->first)->setChecked(true);
        form_->addRow(label, widget);
        return group;
      };
  edge_group_ =
      add_radios("Edge:", edge_widget_,
                 {{static_cast<int>(core::TriggerEdge::kRising), "Rising"},
                  {static_cast<int>(core::TriggerEdge::kFalling), "Falling"}});

  spin_level_ = new SpinBox(this);
  // Apply typed levels on Enter or focus out, not on every keystroke.
  spin_level_->setKeyboardTracking(false);
  SetLevelQuantity(level_quantity_);
  form_->addRow("Level:", spin_level_);

  mode_group_ =
      add_radios("Mode:", mode_widget_,
                 {{static_cast<int>(core::TriggerMode::kAuto), "Auto"},
                  {static_cast<int>(core::TriggerMode::kNormal), "Normal"}});
  mode_group_->button(static_cast<int>(core::TriggerMode::kAuto))
      ->setToolTip("Show the latest samples when the trigger does not fire");
  mode_group_->button(static_cast<int>(core::TriggerMode::kNormal))
      ->setToolTip("Keep the last triggered samples until the trigger fires");

  connect(combo_source_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this] {
            UpdateControls();
            Apply();
          });
  connect(edge_group_, &QButtonGroup::idClicked, this, &TriggerPanel::Apply);
  connect(spin_level_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &TriggerPanel::Apply);
  connect(mode_group_, &QButtonGroup::idClicked, this, &TriggerPanel::Apply);

  UpdateControls();
}

void TriggerPanel::UpdateControls() {
  const int measurement = combo_source_->currentData().toInt();
  const bool enabled = measurement != core::kNoTrigger;
  for (QWidget* field : std::initializer_list<QWidget*>{
           edge_widget_, spin_level_, mode_widget_}) {
    SetRowEnabled(form_, field, enabled);
  }

  // Show the level in the unit of the source quantity. A level in the other
  // quantity is meaningless, so it restarts mid-range. Only change the unit
  // when the quantity changes, so a level being typed is not reformatted.
  if (!enabled) return;
  const core::Quantity quantity = core::kMeasurements[measurement].quantity;
  if (quantity != level_quantity_) SetLevelQuantity(quantity);
}

void TriggerPanel::SetLevelQuantity(core::Quantity quantity) {
  level_quantity_ = quantity;
  const QSignalBlocker blocker(spin_level_);
  spin_level_->SetQuantity(quantity);
  // Start mid-range: at the bottom of the range a rising edge never arms, as
  // the value cannot go below it by the hysteresis.
  spin_level_->setValue((spin_level_->minimum() + spin_level_->maximum()) / 2);
}

void TriggerPanel::Apply() {
  device_.SetTrigger({
      .measurement = combo_source_->currentData().toInt(),
      .edge = static_cast<core::TriggerEdge>(edge_group_->checkedId()),
      .level = static_cast<float>(spin_level_->GetQuantityValue()),
      .mode = static_cast<core::TriggerMode>(mode_group_->checkedId()),
  });
  emit TriggerChanged();
}

OscilloscopePanel::OscilloscopePanel(core::Device& device, QWidget* parent)
    : QWidget(parent) {
  auto* main_layout = new QVBoxLayout(this);
  SetPanelMargins(main_layout);
  main_layout->setSpacing(kRowSpacing);

  x_axis_panel_ = new XAxisPanel(this);
  main_layout->addWidget(x_axis_panel_);
  y_axis_panel_ = new YAxisPanel(this);
  main_layout->addWidget(y_axis_panel_);
  connect(x_axis_panel_, &XAxisPanel::XMeasurementChanged, y_axis_panel_,
          &YAxisPanel::SetXMeasurement);

  // Keep the trigger group at the bottom.
  main_layout->addStretch();
  auto* trigger_panel = new TriggerPanel(device, this);
  main_layout->addWidget(trigger_panel);
  connect(trigger_panel, &TriggerPanel::TriggerChanged, this,
          &OscilloscopePanel::SettingsChanged);
}

}  // namespace m1k_scope::ui
