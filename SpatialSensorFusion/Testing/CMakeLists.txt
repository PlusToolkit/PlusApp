# Testing
SET( TestDataDir ${PLUSLIB_DATA_DIR}/TestImages )
SET( ConfigFilesDir ${PLUSLIB_DATA_DIR}/ConfigFiles )

ADD_TEST(SpatialSensorFusionTest
  ${PLUS_EXECUTABLE_OUTPUT_PATH}/SpatialSensorFusion
  --ahrs-algo=MADGWICK_IMU
  --ahrs-algo-gain 1.5 
  --initial-gain 1
  --initial-repeated-frame-number=1000
  --input-seq-file=${TestDataDir}/SpatialSensorFusionTestInput.igs.mha
  --output-seq-file=${TestDataDir}/SpatialSensorFusionTestOutput.igs.mha
  --baseline-seq-file=${TestDataDir}/SpatialSensorFusionTestBaseline.igs.mha
  --west-axis-index=1
  )

SET_TESTS_PROPERTIES( SpatialSensorFusionTest PROPERTIES FAIL_REGULAR_EXPRESSION "ERROR;WARNING" )