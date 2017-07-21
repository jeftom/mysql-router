/*
  Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "gmock/gmock.h"
#include "router_component_test.h"

Path g_origin_path;

class RouterRoutingTest : public RouterComponentTest, public ::testing::Test {
 protected:
  virtual void SetUp() {
    set_origin(g_origin_path);
    RouterComponentTest::SetUp();
  }

  TcpPortPool port_pool_;
};

TEST_F(RouterRoutingTest, RoutingOk) {
  const auto server_port = port_pool_.get_next_available();
  const auto router_port = port_pool_.get_next_available();

  const std::string json_stmts = get_data_dir().join("bootstrapper.json").str();
  const std::string bootstrap_dir = get_tmp_dir();

  // launch the server mock for bootstrapping
  auto server_mock = launch_mysql_server_mock(json_stmts, server_port);

  const std::string routing_section =
                      "[routing:basic]\n"
                      "bind_port = " + std::to_string(router_port) + "\n"
                      "mode = read-write\n"
                      "destinations = 127.0.0.1:" + std::to_string(server_port) + "\n";

  std::string conf_file = create_config_file(routing_section);


  // launch the router with simple static routing configuration
  auto router_static = launch_router("-c " +  conf_file);

  // wait for both to begin accepting the connections
  bool ready = wait_for_port_ready(server_port, 1000);
  EXPECT_TRUE(ready) << server_mock.get_full_output();
  ready = wait_for_port_ready(router_port, 1000);
  EXPECT_TRUE(ready) << router_static.get_full_output();

  // launch another router to do the bootstrap connecting to the mock server
  // via first router instance
  std::shared_ptr<void> exit_guard(nullptr, [&](void*){purge_dir(bootstrap_dir);});
  auto router_bootstrapping = launch_router("--bootstrap=localhost:" + std::to_string(router_port)
                                            + " -d " +  bootstrap_dir);

  router_bootstrapping.register_response("Please enter MySQL password for root: ", "fake-pass\n");

  EXPECT_TRUE(router_bootstrapping.expect_output(
    "MySQL Router  has now been configured for the InnoDB cluster 'test'")
  ) << "bootstrap output: " << router_bootstrapping.get_full_output() << std::endl
    << "routing output: "<< router_static.get_full_output() << std::endl
    << "server output: "<< server_mock.get_full_output() << std::endl;

  EXPECT_EQ(router_bootstrapping.wait_for_exit(), 0
  ) << "bootstrap output: " << router_bootstrapping.get_full_output();
}


int main(int argc, char *argv[]) {
  init_windows_sockets();
  g_origin_path = Path(argv[0]).dirname();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}