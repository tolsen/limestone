-- PostgreSQL database dump

SET client_encoding = 'UTF8';
--SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;
--SET search_path = public, pg_catalog;
SET default_tablespace = '';
SET default_with_oids = false;

CREATE TABLE aces (
    id integer NOT NULL,
    grantdeny character(1) NOT NULL,
    protected character(1) DEFAULT 'f'::bpchar NOT NULL,
    resource_id bigint NOT NULL,
    principal_id bigint NOT NULL,
    property_namespace_id bigint,
    property_name character varying(4096) 
);

CREATE SEQUENCE aces_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('aces_id_seq', 9, true);

CREATE TABLE acl_privileges (
    id integer NOT NULL,
    name character varying(255),
    abstract character(1) DEFAULT 'f'::bpchar NOT NULL,
    parent_id bigint,
    lft bigint,
    rgt bigint
);

CREATE SEQUENCE acl_privileges_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('acl_privileges_id_seq', 11, true);

CREATE TABLE autoversion_types (
    id integer NOT NULL,
    name character varying(255) NOT NULL
);

CREATE SEQUENCE autoversion_types_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('autoversion_types_id_seq', 5, true);

CREATE TABLE binds (
    id integer NOT NULL,
    name character varying(767) NOT NULL,
    collection_id bigint NOT NULL,
    resource_id bigint NOT NULL,
    updated_at timestamp without time zone
);

CREATE SEQUENCE binds_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('binds_id_seq', 7, true);

CREATE TABLE binds_locks (
    lock_id bigint NOT NULL,
    bind_id bigint NOT NULL
);

CREATE TABLE cleanup (
    id integer NOT NULL,
    resource_id bigint NOT NULL
);

CREATE SEQUENCE cleanup_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('cleanup_id_seq', 1, false);

CREATE TABLE dav_aces_privileges (
    ace_id bigint NOT NULL,
    privilege_id bigint NOT NULL
);

CREATE TABLE dav_acl_inheritance (
    resource_id bigint NOT NULL,
    parent_id bigint,
    lft bigint DEFAULT (1)::bigint,
    rgt bigint DEFAULT (2)::bigint
);

CREATE TABLE group_members (
    id integer NOT NULL,
    group_id bigint NOT NULL,
    member_id bigint NOT NULL
);

CREATE SEQUENCE group_members_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('group_members_id_seq', 4, true);

CREATE TABLE locks (
    id integer NOT NULL,
    uuid character(32) NOT NULL,
    resource_id bigint NOT NULL,
    owner_id bigint NOT NULL,
    form character(1) DEFAULT 'X'::bpchar NOT NULL,
    depth bigint DEFAULT (0)::bigint NOT NULL,
    expires_at timestamp without time zone NOT NULL,
    owner_info text NOT NULL,
    lockroot text NOT NULL
);

CREATE SEQUENCE locks_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('locks_id_seq', 1, false);

CREATE TABLE locks_resources (
    lock_id bigint NOT NULL,
    resource_id bigint NOT NULL
);

CREATE TABLE media (
    resource_id bigint NOT NULL,
    size bigint NOT NULL,
    mimetype character varying(255),
    sha1 character(40) NOT NULL,
    updated_at timestamp without time zone NOT NULL
);

CREATE TABLE namespaces (
    id integer NOT NULL,
    name character varying(4096) NOT NULL
);

CREATE SEQUENCE namespaces_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('namespaces_id_seq', 1, false);

CREATE TABLE principals (
    resource_id bigint NOT NULL,
    name character varying(1024) NOT NULL
);

CREATE TABLE quota (
    principal_id bigint NOT NULL,
    used_quota bigint DEFAULT (0)::bigint NOT NULL CHECK (used_quota >= 0),
    total_quota bigint DEFAULT (0)::bigint NOT NULL
);

CREATE TABLE properties (
    id integer NOT NULL,
    namespace_id bigint NOT NULL,
    name character varying(4096) NOT NULL,
    resource_id bigint NOT NULL,
    xmlinfo text NOT NULL,
    value text NOT NULL
);

CREATE SEQUENCE properties_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('properties_id_seq', 1, false);

CREATE TABLE resource_types (
    id integer NOT NULL,
    name character varying(255) NOT NULL
);

CREATE SEQUENCE resource_types_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('resource_types_id_seq', 13, true);

CREATE TABLE resources (
    id integer NOT NULL,
    uuid character(32) NOT NULL,
    created_at timestamp without time zone NOT NULL,
    displayname character varying(1024),
    contentlanguage character varying(255) NOT NULL,
    "comment" text,
    type_id bigint NOT NULL,
    owner_id bigint NOT NULL,
    creator_id bigint NOT NULL
);

CREATE SEQUENCE resources_id_seq
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;

SELECT pg_catalog.setval('resources_id_seq', 11, true);

CREATE TABLE transitive_group_members (
    transitive_group_id bigint NOT NULL,
    transitive_member_id bigint NOT NULL,
    transitive_count bigint NOT NULL
);

CREATE TABLE users (
    principal_id bigint NOT NULL,
    pwhash character(32) NOT NULL
);

CREATE TABLE vcrs (
    resource_id bigint NOT NULL,
    checked_id bigint NOT NULL,
    vhr_id bigint NOT NULL,
    checked_state character(1) DEFAULT 'I'::bpchar NOT NULL,
    version_type bigint DEFAULT (1)::bigint NOT NULL,
    checkin_on_unlock bigint DEFAULT (0)::bigint NOT NULL
);

CREATE TABLE versions (
    resource_id bigint NOT NULL,
    number bigint NOT NULL,
    vcr_id bigint,
    vhr_id bigint NOT NULL
);

CREATE TABLE vhrs (
    resource_id bigint NOT NULL,
    root_version_id bigint NOT NULL
);

ALTER TABLE aces ALTER COLUMN id SET DEFAULT nextval('aces_id_seq'::regclass);

ALTER TABLE acl_privileges ALTER COLUMN id SET DEFAULT nextval('acl_privileges_id_seq'::regclass);

ALTER TABLE autoversion_types ALTER COLUMN id SET DEFAULT nextval('autoversion_types_id_seq'::regclass);

ALTER TABLE binds ALTER COLUMN id SET DEFAULT nextval('binds_id_seq'::regclass);

ALTER TABLE cleanup ALTER COLUMN id SET DEFAULT nextval('cleanup_id_seq'::regclass);

ALTER TABLE group_members ALTER COLUMN id SET DEFAULT nextval('group_members_id_seq'::regclass);

ALTER TABLE locks ALTER COLUMN id SET DEFAULT nextval('locks_id_seq'::regclass);

ALTER TABLE namespaces ALTER COLUMN id SET DEFAULT nextval('namespaces_id_seq'::regclass);

ALTER TABLE properties ALTER COLUMN id SET DEFAULT nextval('properties_id_seq'::regclass);

ALTER TABLE resource_types ALTER COLUMN id SET DEFAULT nextval('resource_types_id_seq'::regclass);

ALTER TABLE resources ALTER COLUMN id SET DEFAULT nextval('resources_id_seq'::regclass);

COPY aces (id, grantdeny, protected, resource_id, principal_id, property_namespace_id, property_name) FROM stdin;
1	G	t	2	1	\N	\N
2	G	f	3	3	\N	\N
3	G	f	4	4	\N	\N
4	G	f	5	5	\N	\N
5	G	t	6	3	\N	\N
6	G	t	7	4	\N	\N
7	G	t	8	1	\N	\N
8	G	t	7	3	\N	\N
\.

COPY acl_privileges (id, name, abstract, parent_id, lft, rgt) FROM stdin;
1	all	f	\N	1	22
2	read	f	1	2	3
3	read-acl	f	1	4	5
4	read-current-user-privilege-set	f	1	6	7
5	write-acl	f	1	8	9
6	unlock	f	1	10	11
7	write	f	1	12	21
8	write-properties	f	7	13	14
9	write-content	f	7	15	16
10	bind	f	7	17	18
11	unbind	f	7	19	20
\.

COPY autoversion_types (id, name) FROM stdin;
1	DAV:checkout-checkin
2	DAV:checkout-unlocked-checkin
3	DAV:checkout
4	DAV:locked-checkout
5	no-auto-version
\.

COPY binds (id, name, collection_id, resource_id, updated_at) FROM stdin;
1	groups	2	6	2006-07-14 13:15:49
2	users	2	7	2006-07-14 13:15:49
3	limestone	7	1	2006-07-14 13:15:49
4	home	2	8	2006-07-14 13:15:49
\.

COPY binds_locks (lock_id, bind_id) FROM stdin;
\.

COPY cleanup (id, resource_id) FROM stdin;
\.

COPY dav_aces_privileges (ace_id, privilege_id) FROM stdin;
1	1
2	1
3	1
4	1
5	2
6	2
7	1
8	10
\.

COPY dav_acl_inheritance (resource_id, parent_id, lft, rgt) FROM stdin;
2	2	1	6
7	2	2	3
8	2	4	5
\.

COPY group_members (id, group_id, member_id) FROM stdin;
1	3	4
2	3	5
\.

COPY locks (id, uuid, resource_id, owner_id, form, depth, expires_at, owner_info, lockroot) FROM stdin;
\.

COPY locks_resources (lock_id, resource_id) FROM stdin;
\.

COPY media (resource_id, size, mimetype, sha1, updated_at) FROM stdin;
\.

COPY namespaces (id, name) FROM stdin;
\.

COPY principals (resource_id, name) FROM stdin;
1	limestone
3	all
4	authenticated
5	unauthenticated
\.

COPY quota (principal_id, used_quota, total_quota) FROM stdin;
1	0	0
3	0	1073741824
4	0	1073741824
5	0	1073741824
\.

COPY properties (id, namespace_id, name, resource_id, value) FROM stdin;
\.

COPY resource_types (id, name) FROM stdin;
1	Resource
2	Collection
3	Principal
4	User
5	Group
6	Redirect
7	Medium
8	MediumVersion
9	VersionedMedium
10	VersionHistory
11	VersionedCollection
12	CollectionVersion
13	LockNull
\.

COPY resources (id, uuid, created_at, displayname, contentlanguage, "comment", type_id, owner_id, creator_id) FROM stdin;
1	95a44c3fb7694e67b7822236765a2fec	2006-07-14 13:15:49	LimeStone	en-US	\N	4	1	1
2	49421d7c1eae44c3b8a74eb61ebc5cc6	2006-07-14 13:15:49	RootDirectory	en-US	\N	2	1	1
3	bd655bf450344f408715bdb212d23614	2006-07-14 13:15:49	All Users	en	\N	3	3	1
4	b5ce953e18b041fab3da4ae4e2d52249	2006-07-14 13:15:49	Authenticated Users	en	\N	3	4	1
5	91ffbc3e22d44110858244ba66522cfb	2006-07-14 13:15:49	Unauthenticated Users	en	\N	4	5	1
6	9f0e35aeeca911db84050b67d6d069da	2006-08-23 16:56:19	groups		\N	2	1	1
7	7467b19cad1b041082053b90ab58c935	2006-08-23 16:56:19	users		\N	2	1	1
8	f467b19cad1b041082053b90ab58c935	2006-08-23 16:56:19	home		\N	2	1	1
\.

COPY transitive_group_members (transitive_group_id, transitive_member_id, transitive_count) FROM stdin;
3	4	1
3	5	1
\.

COPY users (principal_id, pwhash) FROM stdin;
1	f2f2fba55068596de02a6771b8b9d13c
5	4dae264488723b3f68f78a917d605cda
\.

COPY vcrs (resource_id, checked_id, vhr_id, checked_state, version_type, checkin_on_unlock) FROM stdin;
\.

COPY versions (resource_id, number, vcr_id, vhr_id) FROM stdin;
\.

COPY vhrs (resource_id, root_version_id) FROM stdin;
\.

ALTER TABLE ONLY aces
    ADD CONSTRAINT aces_pkey PRIMARY KEY (id);

ALTER TABLE ONLY acl_privileges
    ADD CONSTRAINT acl_privileges_pkey PRIMARY KEY (id);

ALTER TABLE ONLY autoversion_types
    ADD CONSTRAINT autoversion_types_pkey PRIMARY KEY (id);

ALTER TABLE ONLY binds
    ADD CONSTRAINT binds_collection_id_key UNIQUE (collection_id, name);

ALTER TABLE ONLY binds
    ADD CONSTRAINT binds_pkey PRIMARY KEY (id);

ALTER TABLE ONLY cleanup
    ADD CONSTRAINT cleanup_pkey PRIMARY KEY (id);

ALTER TABLE ONLY dav_aces_privileges
    ADD CONSTRAINT dav_aces_privileges_pkey PRIMARY KEY (ace_id, privilege_id);

--ALTER TABLE ONLY cleanup
--    ADD CONSTRAINT fk_cleanup_resource UNIQUE (resource_id);

ALTER TABLE ONLY dav_acl_inheritance
    ADD CONSTRAINT fk_ip_resource UNIQUE (resource_id);

ALTER TABLE ONLY principals
    ADD CONSTRAINT fk_pp_resource UNIQUE (resource_id);

ALTER TABLE ONLY quota
    ADD CONSTRAINT pk_quota_principal UNIQUE (principal_id);

ALTER TABLE ONLY users
    ADD CONSTRAINT fk_us_principal UNIQUE (principal_id);

ALTER TABLE ONLY vcrs
    ADD CONSTRAINT fk_vc_resource UNIQUE (resource_id);

ALTER TABLE ONLY versions
    ADD CONSTRAINT fk_ve_resource UNIQUE (resource_id);

ALTER TABLE ONLY vhrs
    ADD CONSTRAINT fk_vh_resource UNIQUE (resource_id);

ALTER TABLE ONLY group_members
    ADD CONSTRAINT group_members_pkey PRIMARY KEY (id);

ALTER TABLE ONLY locks
    ADD CONSTRAINT locks_pkey PRIMARY KEY (id);

ALTER TABLE ONLY locks_resources
    ADD CONSTRAINT locks_resources_pkey PRIMARY KEY (lock_id, resource_id);

ALTER TABLE ONLY namespaces
    ADD CONSTRAINT namespaces_pkey PRIMARY KEY (id);

ALTER TABLE ONLY properties
    ADD CONSTRAINT properties_pkey PRIMARY KEY (id);

ALTER TABLE ONLY resource_types
    ADD CONSTRAINT resource_types_pkey PRIMARY KEY (id);

ALTER TABLE ONLY resources
    ADD CONSTRAINT resources_pkey PRIMARY KEY (id);

-- FIXME: find a way to do 'NULLable' FKs and uncomment this
--ALTER TABLE ONLY aces
--    ADD CONSTRAINT aces_principal_id_fkey FOREIGN KEY (principal_id) REFERENCES principals(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY aces
    ADD CONSTRAINT aces_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY binds
    ADD CONSTRAINT binds_collection_id_fkey FOREIGN KEY (collection_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY binds_locks
    ADD CONSTRAINT binds_locks_bind_id_fkey FOREIGN KEY (bind_id) REFERENCES binds(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY binds_locks
    ADD CONSTRAINT binds_locks_lock_id_fkey FOREIGN KEY (lock_id) REFERENCES locks(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY binds
    ADD CONSTRAINT binds_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY cleanup
    ADD CONSTRAINT cleanup_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY dav_aces_privileges
    ADD CONSTRAINT dav_aces_privileges_ace_id_fkey FOREIGN KEY (ace_id) REFERENCES aces(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY dav_aces_privileges
    ADD CONSTRAINT dav_aces_privileges_privilege_id_fkey FOREIGN KEY (privilege_id) REFERENCES acl_privileges(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY dav_acl_inheritance
    ADD CONSTRAINT dav_acl_inheritance_parent_id_fkey FOREIGN KEY (parent_id) REFERENCES dav_acl_inheritance(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY dav_acl_inheritance
    ADD CONSTRAINT dav_acl_inheritance_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY versions
    ADD CONSTRAINT fk_ve_vc_resource FOREIGN KEY (vcr_id) REFERENCES vcrs(resource_id) ON DELETE SET NULL DEFERRABLE;

ALTER TABLE ONLY vhrs
    ADD CONSTRAINT fk_vh_root_version FOREIGN KEY (root_version_id) REFERENCES versions(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY group_members
    ADD CONSTRAINT group_members_group_id_fkey FOREIGN KEY (group_id) REFERENCES principals(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY group_members
    ADD CONSTRAINT group_members_member_id_fkey FOREIGN KEY (member_id) REFERENCES principals(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY locks
    ADD CONSTRAINT locks_owner_id_fkey FOREIGN KEY (owner_id) REFERENCES users(principal_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY locks
    ADD CONSTRAINT locks_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY locks_resources
    ADD CONSTRAINT locks_resources_lock_id_fkey FOREIGN KEY (lock_id) REFERENCES locks(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY locks_resources
    ADD CONSTRAINT locks_resources_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY media
    ADD CONSTRAINT media_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY principals
    ADD CONSTRAINT principals_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY quota
    ADD CONSTRAINT fk_quota_pp_resource FOREIGN KEY (principal_id) REFERENCES principals(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY properties
    ADD CONSTRAINT properties_namespace_id_fkey FOREIGN KEY (namespace_id) REFERENCES namespaces(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY properties
    ADD CONSTRAINT properties_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY resources
    ADD CONSTRAINT resources_creator_id_fkey FOREIGN KEY (creator_id) REFERENCES principals(resource_id) DEFERRABLE;

ALTER TABLE ONLY resources
    ADD CONSTRAINT resources_owner_id_fkey FOREIGN KEY (owner_id) REFERENCES principals(resource_id) DEFERRABLE;

ALTER TABLE ONLY resources
    ADD CONSTRAINT resources_type_id_fkey FOREIGN KEY (type_id) REFERENCES resource_types(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY transitive_group_members
    ADD CONSTRAINT transitive_group_members_transitive_group_id_fkey FOREIGN KEY (transitive_group_id) REFERENCES principals(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY transitive_group_members
    ADD CONSTRAINT transitive_group_members_transitive_member_id_fkey FOREIGN KEY (transitive_member_id) REFERENCES principals(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY users
    ADD CONSTRAINT users_principal_id_fkey FOREIGN KEY (principal_id) REFERENCES principals(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY vcrs
    ADD CONSTRAINT vcrs_checked_id_fkey FOREIGN KEY (checked_id) REFERENCES versions(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY vcrs
    ADD CONSTRAINT vcrs_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY vcrs
    ADD CONSTRAINT vcrs_vhr_id_fkey FOREIGN KEY (vhr_id) REFERENCES vhrs(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY versions
    ADD CONSTRAINT versions_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY versions
    ADD CONSTRAINT versions_vhr_id_fkey FOREIGN KEY (vhr_id) REFERENCES vhrs(resource_id) ON DELETE CASCADE DEFERRABLE;

ALTER TABLE ONLY vhrs
    ADD CONSTRAINT vhrs_resource_id_fkey FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE DEFERRABLE;

CREATE RULE update_quota_media_insert AS ON INSERT TO media
    DO UPDATE quota SET used_quota = used_quota + NEW.size WHERE principal_id = (SELECT owner_id FROM resources WHERE id = NEW.resource_id);

CREATE RULE update_quota_media_update AS ON UPDATE TO media
    DO UPDATE quota SET used_quota = used_quota + NEW.size - OLD.size WHERE principal_id = (SELECT owner_id FROM resources WHERE id = NEW.resource_id);

CREATE RULE update_quota_media_delete AS ON DELETE TO media
    DO UPDATE quota SET used_quota = used_quota - OLD.size WHERE principal_id = (SELECT owner_id FROM resources WHERE id = OLD.resource_id);

-- PostgreSQL database dump complete

