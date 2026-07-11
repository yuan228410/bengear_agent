#include "cli/render/runtime_presenter.hpp"
#include "test_framework.hpp"

#include <sstream>
#include <string>

TEST(CliRuntimePresenterTest, RendersStructuredRuntimeEventsWithoutOwningExecution) {
    std::ostringstream out;
    ben_gear::cli::RuntimePresenter presenter(out);

    ben_gear::core::RuntimeEvent event;
    event.request_id = "req-1";
    event.operation_id = "cli.single_request";
    event.step_id = "execute";
    event.kind = ben_gear::core::RuntimeEventKind::step_started;
    event.status = ben_gear::core::RuntimeStatus::running;
    event.message = "running";

    presenter.on_event(event);

    const auto text = out.str();
    EXPECT_NE(text.find("step_started"), std::string::npos);
    EXPECT_NE(text.find("status=running"), std::string::npos);
    EXPECT_NE(text.find("step=execute"), std::string::npos);
}
