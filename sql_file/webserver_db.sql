/*
 Navicat Premium Data Transfer

 Source Server         : Ubuntu20.04
 Source Server Type    : MySQL
 Source Server Version : 80034
 Source Host           : 192.168.184.10:3306
 Source Schema         : webserver_db

 Target Server Type    : MySQL
 Target Server Version : 80034
 File Encoding         : 65001

 Date: 23/09/2023 19:43:05
*/

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- ----------------------------
-- Table structure for user
-- ----------------------------
DROP TABLE IF EXISTS `user`;
CREATE TABLE `user`  (
  `username` varchar(50) CHARACTER SET latin1 COLLATE latin1_swedish_ci NOT NULL COMMENT '唯一用户名',
  `password` varchar(50) CHARACTER SET latin1 COLLATE latin1_swedish_ci NOT NULL COMMENT '用户密码',
  PRIMARY KEY (`username`) USING BTREE
) ENGINE = InnoDB CHARACTER SET = latin1 COLLATE = latin1_swedish_ci ROW_FORMAT = Dynamic;

-- ----------------------------
-- Records of user
-- ----------------------------
INSERT INTO `user` VALUES ('1111', '1111');
INSERT INTO `user` VALUES ('567', '1234');
INSERT INTO `user` VALUES ('hhhh', '1234');
INSERT INTO `user` VALUES ('jay', '1234');
INSERT INTO `user` VALUES ('xiaoming', '1111');
INSERT INTO `user` VALUES ('xiaozhang', '1111');
INSERT INTO `user` VALUES ('yangfan', '1111');
INSERT INTO `user` VALUES ('yyy', '123');
INSERT INTO `user` VALUES ('zhang', '1234');
INSERT INTO `user` VALUES ('zhang3', '1234');
INSERT INTO `user` VALUES ('zhangfei', '1234');
INSERT INTO `user` VALUES ('zhangsan', '1234');

SET FOREIGN_KEY_CHECKS = 1;
