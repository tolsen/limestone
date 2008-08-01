-- ====================================================================
-- Copyright 2007 Lime Spot LLC

-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at

--     http://www.apache.org/licenses/LICENSE-2.0

-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

-- ====================================================================

-- MySQL dump 10.10
--
-- Host: localhost    Database: limestone
-- ------------------------------------------------------
-- Server version	5.0.22-Debian_0ubuntu6.06-log

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Table structure for table `aces`
--

DROP TABLE IF EXISTS `aces`;
CREATE TABLE `aces` (
  `id` int(11) NOT NULL auto_increment,
  `grantdeny` char(1) NOT NULL,
  `protected` char(1) NOT NULL default 'f',
  `resource_id` int(11) NOT NULL,
  `principal_id` int(11) NOT NULL,
  `property_namespace_id` int(11) NOT NULL,
  `property_name` varchar(4096),
  PRIMARY KEY  (`id`),
  KEY `fk_rp_resource` (`resource_id`),
--  KEY `fk_rp_principal` (`principal_id`),
--  CONSTRAINT `fk_rp_principal` FOREIGN KEY (`principal_id`) REFERENCES `principals` (`resource_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_rp_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `aces`
--


/*!40000 ALTER TABLE `aces` DISABLE KEYS */;
LOCK TABLES `aces` WRITE;
INSERT INTO `aces` VALUES (1,'G','t',2,1,NULL,NULL),(2,'G','t',3,3,NULL,NULL),(3,'G','t',4,4,NULL,NULL),(4,'G','t',5,5,NULL,NULL),(5,'G','t',6,3,NULL,NULL),(6,'G','t',7,4,NULL,NULL),(7,'G','t',8,1,NULL,NULL),(8, 'G', 't', 7, 3, NULL, NULL);
UNLOCK TABLES;
/*!40000 ALTER TABLE `aces` ENABLE KEYS */;

DROP TABLE IF EXISTS `dav_aces_privileges`;
CREATE TABLE `dav_aces_privileges` (
   `ace_id` int(11) NOT NULL,
   `privilege_id` int(11) NOT NULL,
   PRIMARY KEY (ace_id, privilege_id),

   KEY `fk_dav_ap_ace` (`ace_id`),
   KEY `fk_dav_ap_pr` (`privilege_id`),
   CONSTRAINT `fk_dav_ap_ace` FOREIGN KEY (`ace_id`) REFERENCES `aces` (`id`) ON DELETE CASCADE,
   CONSTRAINT `fk_dav_ap_pr` FOREIGN KEY (`privilege_id`) REFERENCES `acl_privileges` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

LOCK TABLES `dav_aces_privileges` WRITE;
INSERT INTO `dav_aces_privileges` VALUES (1,1),(2,1),(3,1),(4,1),(5,2),(6,2),(7,1),(8,10);
UNLOCK TABLES;

--
-- Table structure for table `acl_privileges`
--

DROP TABLE IF EXISTS `acl_privileges`;
CREATE TABLE `acl_privileges` (
  `id` int(11) NOT NULL auto_increment,
  `name` varchar(255) default NULL,
  `abstract` char(1) NOT NULL default 'f',
  `parent_id` int(11) default NULL,
  `lft` int(11) default NULL,
  `rgt` int(11) default NULL,
  PRIMARY KEY  (`id`),
  KEY `fk_dav_pr_parent` (`parent_id`),
  -- CONSTRAINT `fk_dav_pr_parent` FOREIGN KEY ( `parent_id` ) REFERENCES `dav_privileges` (`id`),
  INDEX `ix_dav_pr_lft` (`lft`),
  INDEX `ix_dav_pr_rgt` (`rgt`)

) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `acl_privileges`
--


/*!40000 ALTER TABLE `acl_privileges` DISABLE KEYS */;
LOCK TABLES `acl_privileges` WRITE;
INSERT INTO `acl_privileges` VALUES (1,'all','f',NULL,1,22),(2,'read','f',1,2,3),(3,'read-acl','f',1,4,5),(4,'read-current-user-privilege-set','f',1,6,7),(5,'write-acl','f',1,8,9),(6,'unlock','f',1,10,11),(7,'write','f',1,12,21),(8,'write-properties','f',7,13,14),(9,'write-content','f',7,15,16),(10,'bind','f',7,17,18),(11,'unbind','f',7,19,20);
UNLOCK TABLES;
/*!40000 ALTER TABLE `acl_privileges` ENABLE KEYS */;

--
-- Table structure for table `aggregate_privileges`
--

--DROP TABLE IF EXISTS `aggregate_privileges`;
--CREATE TABLE `aggregate_privileges` (
--  `parent_id` int(11) NOT NULL,
--  `child_id` int(11) NOT NULL,
--  KEY `fk_ap_parent` (`parent_id`),
--  KEY `fk_ap_child` (`child_id`),
--  CONSTRAINT `fk_ap_child` FOREIGN KEY (`child_id`) REFERENCES `acl_privileges` (`id`),
--  CONSTRAINT `fk_ap_parent` FOREIGN KEY (`parent_id`) REFERENCES `acl_privileges` (`id`)
--) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `aggregate_privileges`
--


--/*!40000 ALTER TABLE `aggregate_privileges` DISABLE KEYS */;
--LOCK TABLES `aggregate_privileges` WRITE;
--INSERT INTO `aggregate_privileges` VALUES (1,2),(1,3),(1,4),(1,5),(1,10),(1,11),(5,6),(5,7),(5,8),(5,9);
--UNLOCK TABLES;
--/*!40000 ALTER TABLE `aggregate_privileges` ENABLE KEYS */;

--
-- Table structure for table `autoversion_types`
--

DROP TABLE IF EXISTS `autoversion_types`;
CREATE TABLE `autoversion_types` (
  `id` int(11) NOT NULL auto_increment,
  `name` varchar(255) NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `autoversion_types`
--


/*!40000 ALTER TABLE `autoversion_types` DISABLE KEYS */;
LOCK TABLES `autoversion_types` WRITE;
INSERT INTO `autoversion_types` VALUES (1,'DAV:checkout-checkin'),(2,'DAV:checkout-unlocked-checkin'),(3,'DAV:checkout'),(4,'DAV:locked-checkout'),(5,'no-auto-version');
UNLOCK TABLES;
/*!40000 ALTER TABLE `autoversion_types` ENABLE KEYS */;

--
-- Table structure for table `binds`
--

DROP TABLE IF EXISTS `binds`;
CREATE TABLE `binds` (
  `id` int(11) NOT NULL auto_increment,
  `name` varchar(767) NOT NULL,
  `collection_id` int(11) NOT NULL,
  `resource_id` int(11) NOT NULL,
  `updated_at` datetime,
  PRIMARY KEY  (`id`),
  UNIQUE KEY (`collection_id`,`name`),
  KEY `fk_bi_collection` (`collection_id`),
  KEY `fk_bi_resource` (`resource_id`),
  CONSTRAINT `fk_bi_collection` FOREIGN KEY (`collection_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_bi_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `binds`
--


/*!40000 ALTER TABLE `binds` DISABLE KEYS */;
LOCK TABLES `binds` WRITE;
INSERT INTO `binds` VALUES (1, 'groups', 2, 6, '2006-07-14 13:15:49'), (2,'users',2,7,'2006-07-14 13:15:49'),(3,'limestone',7,1,'2006-07-14 13:15:49'),(4,'home',2,8,'2006-07-14 13:15:49');
UNLOCK TABLES;
/*!40000 ALTER TABLE `binds` ENABLE KEYS */;

--
-- Table structure for table `group_members`
--

DROP TABLE IF EXISTS `group_members`;
CREATE TABLE `group_members` (
  `group_id` int(11) NOT NULL,
  `member_id` int(11) NOT NULL,
  PRIMARY KEY  (group_id, member_id),
  KEY `fk_gm_group` (`group_id`),
  KEY `fk_gm_member` (`member_id`),
  CONSTRAINT `fk_gm_group` FOREIGN KEY (`group_id`) REFERENCES `principals` (`resource_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_gm_member` FOREIGN KEY (`member_id`) REFERENCES `principals` (`resource_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `group_members`
--


/*!40000 ALTER TABLE `group_members` DISABLE KEYS */;
LOCK TABLES `group_members` WRITE;
INSERT INTO `group_members` VALUES (3,4),(3,5);
UNLOCK TABLES;
/*!40000 ALTER TABLE `group_members` ENABLE KEYS */;

--
-- Table structure for table `dav_acl_inheritance`
--

DROP TABLE IF EXISTS `dav_acl_inheritance`;
CREATE TABLE `dav_acl_inheritance` (
  `resource_id` int(11) NOT NULL,

--  `base_id` int(11) default NULL,

  `parent_id` int(11) default NULL,
  `lft` int(11) default '1',
  `rgt` int(11) default '2',

  UNIQUE KEY `fk_ip_resource` (`resource_id`),
--  KEY `fk_ai_base` (`base_id`),
  KEY `fk_ip_parent` (`parent_id`),
  KEY `ui_ip_lft` (`lft`),
  KEY `ui_ip_rgt` (`rgt`),
  CONSTRAINT `fk_ip_parent` FOREIGN KEY (`parent_id`) REFERENCES `dav_acl_inheritance` (`resource_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_ip_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE,

  INDEX `ix_ai_lft` (`lft`),
  INDEX `ix_ai_rgt` (`rgt`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `dav_acl_inheritance`
--


/*!40000 ALTER TABLE `dav_acl_inheritance` DISABLE KEYS */;
LOCK TABLES `dav_acl_inheritance` WRITE;
INSERT INTO `dav_acl_inheritance` VALUES (2,2,1,6),(7,2,2,3),(8,2,4,5);
UNLOCK TABLES;
/*!40000 ALTER TABLE `dav_acl_inheritance` ENABLE KEYS */;

DROP TABLE IF EXISTS `binds_locks`;
CREATE TABLE `binds_locks` (
  `lock_id` int(11) NOT NULL,
  `bind_id` int(11) NOT NULL,
  CONSTRAINT `fk_bl_bind` FOREIGN KEY (`bind_id`) REFERENCES `binds` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_bl_lock` FOREIGN KEY (`lock_id`) REFERENCES `locks` (`id`) ON DELETE CASCADE,

  INDEX `ix_bl_bind_id` (`bind_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Table structure for table `cleanup`
--

DROP TABLE IF EXISTS `cleanup`;
CREATE TABLE `cleanup` (
  `id` int(11) NOT NULL auto_increment,
  `resource_id` int(11) NOT NULL,
  PRIMARY KEY  (`id`),
  INDEX `ix_cl_res_id` (`resource_id`),
  CONSTRAINT `fk_cleanup_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

-- lock to resource join table 
-- (includes indirect as well as direct locks) 
DROP TABLE IF EXISTS `locks_resources`; 
CREATE TABLE `locks_resources` ( 
  `lock_id` int(11) NOT NULL, 
  `resource_id` int(11) NOT NULL, 
  PRIMARY KEY (lock_id,resource_id), 
  INDEX `ix_dav_lr_lo` (`lock_id`), 
  INDEX `ix_dav_lr_re` (`resource_id`), 
  CONSTRAINT `fk_dav_lr_lo` FOREIGN KEY (`lock_id`) REFERENCES `locks` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_dav_lr_re` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1; 

--
-- Table structure for table `locks`
--

DROP TABLE IF EXISTS `locks`;
CREATE TABLE `locks` (
  `id` int(11) NOT NULL auto_increment,
  `uuid` char(33) NOT NULL,
  `resource_id` int(11) NOT NULL,
  `owner_id` int(11) NOT NULL,
  `form` char(1) NOT NULL default 'X',
  `depth` char(1) NOT NULL default '0',
  `expires_at` datetime NOT NULL,
  `owner_info` text NOT NULL,
  `lockroot` text NOT NULL,
  PRIMARY KEY  (`id`),
  INDEX `ix_lk_resource` (`resource_id`),
  INDEX `ix_lk_owner` (`owner_id`),
  CONSTRAINT `fk_lk_owner` FOREIGN KEY (`owner_id`) REFERENCES `users` (`principal_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_lk_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `locks`
--


/*!40000 ALTER TABLE `locks` DISABLE KEYS */;
LOCK TABLES `locks` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `locks` ENABLE KEYS */;

--
-- Table structure for table `media`
--

DROP TABLE IF EXISTS `media`;
CREATE TABLE `media` (
  `resource_id` int(11) NOT NULL,
  `size` int(20) NOT NULL,
  `mimetype` varchar(255) default NULL,
  `sha1` char(41) NOT NULL,
  `updated_at` datetime NOT NULL,
  KEY `fk_fi_resource` (`resource_id`),
  CONSTRAINT `fk_fi_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

DELIMITER |

CREATE TRIGGER update_quota_media_insert AFTER INSERT ON media
  FOR EACH ROW BEGIN
    UPDATE quota SET used_quota = used_quota + NEW.size WHERE principal_id = (SELECT owner_id FROM resources WHERE id = NEW.resource_id);
  END;
|

CREATE TRIGGER update_quota_media_update BEFORE UPDATE ON media
  FOR EACH ROW BEGIN
    UPDATE quota SET used_quota = used_quota + NEW.size - OLD.size WHERE principal_id = (SELECT owner_id FROM resources WHERE id = NEW.resource_id);
  END;
|

CREATE TRIGGER update_quota_media_delete BEFORE DELETE ON media
  FOR EACH ROW BEGIN
    UPDATE quota SET used_quota = used_quota - OLD.size WHERE principal_id = (SELECT owner_id FROM resources WHERE id = OLD.resource_id);
  END;
|

DELIMITER ;
 
--
-- Table structure for table `namespaces`
--

DROP TABLE IF EXISTS `namespaces`;
CREATE TABLE `namespaces` (
  `id` int(11) NOT NULL auto_increment,
  `name` varchar(4096) NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `namespaces`
--


/*!40000 ALTER TABLE `namespaces` DISABLE KEYS */;
LOCK TABLES `namespaces` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `namespaces` ENABLE KEYS */;

--
-- Table structure for table `principals`
--

DROP TABLE IF EXISTS `principals`;
CREATE TABLE `principals` (
  `resource_id` int(11) NOT NULL,
  `name` varchar(1024) NOT NULL,
  UNIQUE KEY `key_pp_resource` (`resource_id`),
  CONSTRAINT `fk_pp_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `principals`
--


/*!40000 ALTER TABLE `principals` DISABLE KEYS */;
LOCK TABLES `principals` WRITE;
INSERT INTO `principals` VALUES (1,'limestone'),(3,'all'),(4,'authenticated'),(5,'unauthenticated');
UNLOCK TABLES;
/*!40000 ALTER TABLE `principals` ENABLE KEYS */;

DROP TABLE IF EXISTS `quota`;
CREATE TABLE `quota` (
  `principal_id` int(11) NOT NULL,
  `used_quota` int(20) NOT NULL default '0',
  `total_quota` int(20) NOT NULL default '0',
  PRIMARY KEY `pk_quota_principal` (`principal_id`),
  CONSTRAINT `fk_quota_pp_resource` FOREIGN KEY (`principal_id`) REFERENCES `principals` (`resource_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;


LOCK TABLES `quota` WRITE;
INSERT INTO `quota` VALUES (1,0,0),(3,0,1073741824),(4,0,1073741824),(5,0,1073741824);
UNLOCK TABLES;

--
-- Table structure for table `properties`
--

DROP TABLE IF EXISTS `properties`;
CREATE TABLE `properties` (
  `id` int(11) NOT NULL auto_increment,
  `namespace_id` int(11) NOT NULL,
  `name` varchar(4096) NOT NULL,
  `resource_id` int(11) NOT NULL,
  `xmlinfo` text NOT NULL,
  `value` text NOT NULL,
  PRIMARY KEY  (`id`),
  INDEX `ix_pr_resource` (`resource_id`),
  INDEX `ix_pr_namespace` (`namespace_id`),
  CONSTRAINT `fk_pr_namespace` FOREIGN KEY (`namespace_id`) REFERENCES `namespaces` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_pr_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `properties`
--


/*!40000 ALTER TABLE `properties` DISABLE KEYS */;
LOCK TABLES `properties` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `properties` ENABLE KEYS */;

--
-- Table structure for table `resource_types`
--

DROP TABLE IF EXISTS `resource_types`;
CREATE TABLE `resource_types` (
  `id` int(11) NOT NULL auto_increment,
  `name` varchar(255) NOT NULL,
  PRIMARY KEY  (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `resource_types`
--


/*!40000 ALTER TABLE `resource_types` DISABLE KEYS */;
LOCK TABLES `resource_types` WRITE;
INSERT INTO `resource_types` VALUES (1,'Resource'),(2,'Collection'),(3,'Principal'),(4,'User'),(5,'Group'),(6,'Redirect'),(7,'Medium'),(8,'MediumVersion'),(9,'VersionedMedium'),(10,'VersionHistory'),(11,'VersionedCollection'),(12,'CollectionVersion'),(13, 'LockNull');
UNLOCK TABLES;
/*!40000 ALTER TABLE `resource_types` ENABLE KEYS */;

--
-- Table structure for table `resources`
--

DROP TABLE IF EXISTS `resources`;
CREATE TABLE `resources` (
  `id` int(11) NOT NULL auto_increment,
  `uuid` char(33) NOT NULL,
  `created_at` datetime NOT NULL,
  `displayname` varchar(1024) default NULL,
  `contentlanguage` varchar(255) NOT NULL,
  `comment` text,
  `type_id` int(11) NOT NULL,
  `owner_id` int(11) NOT NULL,
  `creator_id` int(11) NOT NULL,

-- ACE Inheritance
--  `acl_parent_id` int(11) default NULL,
--  `acl_lft` int(11) default NULL,
--  `acl_rgt` int(11) default NULL,

  PRIMARY KEY  (`id`),
  KEY `fk_re_types` (`type_id`),
  KEY `fk_re_owner` (`owner_id`),
  KEY `fk_re_creator` (`creator_id`),
  CONSTRAINT `fk_re_types` FOREIGN KEY (`type_id`) REFERENCES `resource_types` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_re_creator` FOREIGN KEY (`creator_id`) REFERENCES `principals` (`resource_id`),
  CONSTRAINT `fk_re_owner` FOREIGN KEY (`owner_id`) REFERENCES `principals` (`resource_id`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `resources`
--


/*!40000 ALTER TABLE `resources` DISABLE KEYS */;
LOCK TABLES `resources` WRITE;
INSERT INTO `resources` VALUES (1,'95a44c3fb7694e67b7822236765a2fec','2006-07-14 13:15:49','LimeStone','en-US',NULL,3,1,1),(2,'49421d7c1eae44c3b8a74eb61ebc5cc6','2006-07-14 13:15:49','RootDirectory','en-US',NULL,2,1,1),(3,'bd655bf450344f408715bdb212d23614','2006-07-14 13:15:49','All Users','en',NULL,3,3,1),(4,'b5ce953e18b041fab3da4ae4e2d52249','2006-07-14 13:15:49','Authenticated Users','en',NULL,3,4,1),(5,'91ffbc3e22d44110858244ba66522cfb','2006-07-14 13:15:49','Unauthenticated Users','en',NULL,3,5,1), (6,'9f0e35aeeca911db84050b67d6d069da','2006-08-23 16:56:19', 'groups', NULL, NULL, 2, 1, 1), (7,'87793fac2db6425c8d1d0c44ff44382d','2006-08-09 19:02:05','users','en',NULL,2,1,1),(8,'0c8e74a26a2248a3b87f1aec98fa892c','2006-08-09 19:02:05','home','en',NULL,2,1,1);
UNLOCK TABLES;
/*!40000 ALTER TABLE `resources` ENABLE KEYS */;

--
-- Table structure for table `transitive_group_members`
--

DROP TABLE IF EXISTS `transitive_group_members`;
CREATE TABLE `transitive_group_members` (
  `transitive_group_id` int(11) NOT NULL,
  `transitive_member_id` int(11) NOT NULL,
  `transitive_count` int(11) NOT NULL,
  KEY `fk_tgm_group` (`transitive_group_id`),
  KEY `fk_tgm_member` (`transitive_member_id`),
  CONSTRAINT `fk_tgm_group` FOREIGN KEY (`transitive_group_id`) REFERENCES `principals` (`resource_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_tgm_member` FOREIGN KEY (`transitive_member_id`) REFERENCES `principals` (`resource_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `transitive_group_members`
--


/*!40000 ALTER TABLE `transitive_group_members` DISABLE KEYS */;
LOCK TABLES `transitive_group_members` WRITE;
INSERT INTO `transitive_group_members` VALUES (3,4,1),(3,5,1);
UNLOCK TABLES;
/*!40000 ALTER TABLE `transitive_group_members` ENABLE KEYS */;

--
-- Table structure for table `users`
--

DROP TABLE IF EXISTS `users`;
CREATE TABLE `users` (
  `principal_id` int(11) NOT NULL,
  `pwhash` char(33) NOT NULL,
  UNIQUE KEY `fk_us_principal` (`principal_id`),
  CONSTRAINT `fk_us_principal` FOREIGN KEY (`principal_id`) REFERENCES `principals` (`resource_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `users`
--


/*!40000 ALTER TABLE `users` DISABLE KEYS */;
LOCK TABLES `users` WRITE;
INSERT INTO `users` VALUES (1,'1741007b113bdf89473e144e1dbe375b'), (5,'4dae264488723b3f68f78a917d605cda');
UNLOCK TABLES;
/*!40000 ALTER TABLE `users` ENABLE KEYS */;

--
-- Table structure for table `vcrs`
--

DROP TABLE IF EXISTS `vcrs`;
CREATE TABLE `vcrs` (
  `resource_id` int(11) NOT NULL,
  `checked_id` int(11) NOT NULL,
  `vhr_id` int(11) NOT NULL,
  `checked_state` char(1) NOT NULL default 'I',
  `version_type` int(11) NOT NULL default '1',
  `checkin_on_unlock` int(11) NOT NULL default 0,
  UNIQUE KEY `fk_vc_resource` (`resource_id`),
  KEY `fk_vc_checked` (`checked_id`),
  KEY `fk_vc_vhr` (`vhr_id`),
		
  CONSTRAINT `fk_vc_vhr` FOREIGN KEY (`vhr_id`) REFERENCES `vhrs` (`resource_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_vc_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_vc_vr` FOREIGN KEY (`checked_id`) REFERENCES `versions` (`resource_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `vcrs`
--


/*!40000 ALTER TABLE `vcrs` DISABLE KEYS */;
LOCK TABLES `vcrs` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `vcrs` ENABLE KEYS */;

--
-- Table structure for table `versions`
--

DROP TABLE IF EXISTS `versions`;
CREATE TABLE `versions` (
  `resource_id` int(11) NOT NULL,
  `number` int(11) NOT NULL,
  `vcr_id` int(11) NULL,
  `vhr_id` int(11) NOT NULL,
  UNIQUE KEY `fk_ve_resource` (`resource_id`),
  CONSTRAINT `fk_ve_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_ve_vh_resource` FOREIGN KEY (`vhr_id`) REFERENCES `vhrs` (`resource_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_ve_vc_resource` FOREIGN KEY (`vcr_id`) REFERENCES `vcrs` (`resource_id`) ON DELETE SET NULL

) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `versions`
--


/*!40000 ALTER TABLE `versions` DISABLE KEYS */;
LOCK TABLES `versions` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `versions` ENABLE KEYS */;

--
-- Table structure for table `vhrs`
--

DROP TABLE IF EXISTS `vhrs`;
CREATE TABLE `vhrs` (
  `resource_id` int(11) NOT NULL,
  `root_version_id` int(11) NOT NULL,
  UNIQUE KEY `fk_vh_resource` (`resource_id`),
  KEY `fk_vh_root_version` (`root_version_id`),
  CONSTRAINT `fk_vh_resource` FOREIGN KEY (`resource_id`) REFERENCES `resources` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_vh_root_version` FOREIGN KEY (`root_version_id`) REFERENCES `versions` (`resource_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=latin1;

--
-- Dumping data for table `vhrs`
--


/*!40000 ALTER TABLE `vhrs` DISABLE KEYS */;
LOCK TABLES `vhrs` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `vhrs` ENABLE KEYS */;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

