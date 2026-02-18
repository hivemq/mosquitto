# CMakeLists.txt for mosquitto_pulse plugin
# Provides HiveMQ Pulse integration for topic discovery and governance

set(PLUGIN_NAME mosquitto_pulse)

# Require gRPC and protobuf (provided by parent CMakeLists.txt)
if(NOT TARGET pulse_proto)
	message(FATAL_ERROR "pulse_proto library not found. Ensure WITH_GRPC is enabled.")
endif()

set(SRCLIST
	plugin.cpp
	pulse_client.cpp
	pulse_client.h
	pulse_discovery.c
	pulse_discovery.h
	pulse_data_type_detector.c
	pulse_data_type_detector.h
	pulse_information_model.h
)

set(INCLIST
	"${CJSON_INCLUDE_DIRS}"
	"${mosquitto_SOURCE_DIR}/include"
	"${mosquitto_SOURCE_DIR}/lib"
	"${mosquitto_SOURCE_DIR}/deps"
	"${CMAKE_BINARY_DIR}/pulse/proto/generated"
	"${Protobuf_INCLUDE_DIRS}"
)

set(LINKLIST
	pulse_proto
	cJSON
	OpenSSL::SSL
)

# Add threading library
if(NOT WIN32)
	find_package(Threads REQUIRED)
	list(APPEND LINKLIST Threads::Threads)
endif()

# Use the standard plugin helper macro
add_mosquitto_plugin("${PLUGIN_NAME}" "${SRCLIST}" "${INCLIST}" "${LINKLIST}")

# On macOS, we need to force-load the entire pulse_proto static library
# to ensure all protobuf descriptor symbols are available at runtime.
# Without this, dlopen fails with "symbol not found in flat namespace"
# for transitively-imported proto files like deviation.proto.
#
# We also need to explicitly link the abseil libraries that pulse_proto
# depends on, since force_load doesn't bring in transitive dependencies.
if(APPLE)
	find_package(Protobuf REQUIRED)
	find_package(gRPC REQUIRED)
	find_package(absl REQUIRED)

	# Find grpc++ library
	find_library(GRPCPP_LIBRARY NAMES grpc++ PATHS /opt/homebrew/lib /usr/local/lib)

	# Link protobuf, gRPC, and abseil directly
	target_link_libraries(${PLUGIN_NAME} PRIVATE
		protobuf::libprotobuf
		gRPC::grpc
		${GRPCPP_LIBRARY}
	)

	# Link abseil targets
	foreach(ABSL_TARGET base status strings cord)
		if(TARGET absl::${ABSL_TARGET})
			target_link_libraries(${PLUGIN_NAME} PRIVATE absl::${ABSL_TARGET})
		endif()
	endforeach()

	# abseil log targets (newer versions)
	foreach(ABSL_LOG_TARGET log log_internal_check_op log_internal_conditions log_internal_message)
		if(TARGET absl::${ABSL_LOG_TARGET})
			target_link_libraries(${PLUGIN_NAME} PRIVATE absl::${ABSL_LOG_TARGET})
		endif()
	endforeach()

	# Force-load pulse_proto to include all protobuf descriptor symbols
	target_link_options(${PLUGIN_NAME} PRIVATE
		"-Wl,-force_load,$<TARGET_FILE:pulse_proto>"
	)
endif()

# Need C++17 for the gRPC/protobuf code
set_property(TARGET ${PLUGIN_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${PLUGIN_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
