/*
 * Copyright (C) 2017 Université Clermont Auvergne, CNRS/IN2P3, LPC
 * Author: Valentin NIESS (niess@in2p3.fr)
 *
 * This software is a C library whose purpose is to transport high energy
 * muons or taus in various media.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

/* The PUMAS API. */
#include "pumas.h"

#ifdef _WIN32
/* For rand_s on Windows */
#define _CRT_RAND_S
#endif

/* The C standard library. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  For debugging with gdb, on linux. */
#ifndef GDB_MODE
#define GDB_MODE 0
#endif
#if (GDB_MODE)
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <fenv.h>
#endif

/* Some tuning factors as macros. */
/**
 * Number of schemes to tabulate for the computation of the energy loss.
 *
 * The NO_LOSS scheme has no tabulation and the detailed one uses the same
 * tabulation than the hybrid scheme.
 */
#define N_SCHEMES 2
/**
 * Order of expansion for the computation of the magnetic deflection,
 * when using CSDA in a uniform medium of infinite extension.
 */
#define N_LARMOR_ORDERS 8
/**
 * Number of inelastic processes for which Discrete Energy Losses (DELs) are
 * simulated.
 */
#define N_DEL_PROCESSES 4
/**
 * Default cutoff between Continuous Energy Loss (CEL) and DELs.
 */
#define DEFAULT_CUTOFF 5E-02
/**
 * Default ratio for the elastic scattering.
 */
#define DEFAULT_ELASTIC_RATIO 5E-02
/**
 * Exponent of the differential cross section approximation in Backward
 * Monte-Carlo (BMC).
 */
#define BMC_ALPHA 2.0
/**
 * Maximum path length for Elastic Hard Scattering (EHS) events, in kg/m^-2.
 */
#define EHS_PATH_MAX 1E+09
/**
 * Default accuracy (relative step length limit) of the Monte Carlo integration.
 */
#define DEFAULT_ACCURACY 1E-02
/**
 * Maximum deflection angle for a soft scattering event, in degrees.
 */
#define MAX_SOFT_ANGLE 1E+00
/**
 * Grammage ratio for small steps, in CSDA mode.
 *
 * Below this ratio, the step energy loss is computed from the stopping power,
 * using finite differences. Above, the interpolated grammage integral is used.
 */
#define EPSILON_X 3E-03
/**
 * Minimum step size.
 */
#define STEP_MIN 1E-07
/**
 * Tuning parameters for the tabulation of the DCS
 */
/* Minimum number of nodes on the low side */
#define DCS_MODEL_N_MIN 10
/* Nodes density per decade on the low side */
#define DCS_MODEL_PER_DECADE 15
/* Constant number of nodes on the high side */
#define DCS_MODEL_N_REVERSE 20
/* Cut value between low and high sides */
#define DCS_MODEL_X_REVERSE 0.6
/* Resolution for the high side sampling */
#define DCS_MODEL_DXMIN 1E-03
/* Upper cut for the tabulation */
#define DCS_MODEL_MAX_FRACTION 0.95
/**
 * Minimum kinetic energy for using the DCS tabulation.
 */
#define DCS_MODEL_MIN_KINETIC 10.
/* Some constants, as macros. */
/**
 * Fine-structure constant
 */
#define ALPHA_EM 7.2973525693E-03
/**
 * Planck constant in GeV m.
 */
#define HBAR_C 1.973269804E-16
/**
 * Bohr radius in m.
 */
#define BOHR_RADIUS 0.529177210903E-10
/**
 * The muon decay length in m.
 */
#define MUON_C_TAU 658.654
/**
 * The tau decay length in m.
 */
#define TAU_C_TAU 87.03E-06
/**
 * Larmor magnetic factor in m^-1 GeV/c T^-1.
 */
#define LARMOR_FACTOR 0.299792458
/**
 * The electron mass in GeV/c^2.
 */
#define ELECTRON_MASS 0.510998910E-03
/**
 * The electron classical radius, in m.
 */
#define ELECTRON_RADIUS 2.817940285E-15
/**
 * The muon mass in GeV/c^2.
 */
#define MUON_MASS 0.10565839
/**
 * The tau mass in GeV/c^2.
 */
#define TAU_MASS 1.77682
/**
 * The proton mass in GeV/c^2.
 */
#define PROTON_MASS 0.938272
/**
 * The neutron mass in GeV/c^2.
 */
#define NEUTRON_MASS 0.939565
/**
 * The pion mass in GeV/c^2.
 */
#define PION_MASS 0.13957018
#ifndef M_PI
/**
 * Define pi, if unknown.
 */
#define M_PI 3.14159265358979323846
#endif
/**
 * Avogadro's number
 */
#define AVOGADRO_NUMBER 6.02214076E+23
/**
 * Default Bremsstrahlung model
 */
#define DEFAULT_BREMSSTRAHLUNG "SSR"
/**
 * Default pair production model
 */
#define DEFAULT_PAIR_PRODUCTION "SSR"
/**
 * Default photonuclear model
 */
#define DEFAULT_PHOTONUCLEAR "DRSS"

/* Helper macros for managing errors. */
#define ERROR_INITIALISE(caller)                                               \
        struct error_context error_data = {.code = PUMAS_RETURN_SUCCESS,       \
                .function = (pumas_function_t *)caller };                      \
        struct error_context * error_ = &error_data;

#define ERROR_MESSAGE(rc, message)                                             \
        error_format(error_, rc, __FILE__, __LINE__, message),                 \
            error_raise(error_)

#define ERROR_FORMAT(rc, format, ...)                                          \
        error_format(error_, rc, __FILE__, __LINE__, format, __VA_ARGS__),     \
            error_raise(error_)

#define ERROR_REGISTER(rc, message)                                            \
        error_format(error_, rc, __FILE__, __LINE__, message)

#define ERROR_VREGISTER(rc, format, ...)                                       \
        error_format(error_, rc, __FILE__, __LINE__, format, __VA_ARGS__)

#define ERROR_RAISE() error_raise(error_)

#define ERROR_NULL_PHYSICS()                                                   \
        ERROR_MESSAGE(PUMAS_RETURN_PHYSICS_ERROR,                              \
            "a NULL physics pointer was provided")

#define ERROR_NOT_INITIALISED()                                                \
        ERROR_MESSAGE(PUMAS_RETURN_PHYSICS_ERROR,                              \
            "the Physics has not been initialised")

#define ERROR_INVALID_SCHEME(scheme)                                           \
        ERROR_FORMAT(PUMAS_RETURN_INDEX_ERROR,                                 \
            "invalid energy loss scheme [%d]", scheme)

#define ERROR_INVALID_ELEMENT(element)                                         \
        ERROR_FORMAT(                                                          \
            PUMAS_RETURN_INDEX_ERROR, "invalid element index [%d]", element)

#define ERROR_INVALID_MATERIAL(material)                                       \
        ERROR_FORMAT(                                                          \
            PUMAS_RETURN_INDEX_ERROR, "invalid material index [%d]", material)

#define ERROR_REGISTER_MEMORY()                                                \
        ERROR_REGISTER(PUMAS_RETURN_MEMORY_ERROR, "could not allocate memory")

#define ERROR_REGISTER_EOF(path)                                               \
        ERROR_VREGISTER(PUMAS_RETURN_END_OF_FILE,                              \
            "abnormal end of file when parsing `%s'", path)

#define ERROR_REGISTER_UNEXPECTED_TAG(tag, path, line)                         \
        ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,                             \
            "unexpected XML tag `%s' [@%s:%d]", tag, path, line)

#define ERROR_REGISTER_TOO_LONG(path, line)                                    \
        ERROR_VREGISTER(PUMAS_RETURN_TOO_LONG,                                 \
            "XML node is too long [@%s:%d]", path, line)

#define ERROR_REGISTER_INVALID_XML_VALUE(value, path, line)                    \
        ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,                             \
            "invalid XML value `%s' [@%s:%d]", value, path, line)

#define ERROR_REGISTER_INVALID_XML_ATTRIBUTE(attribute, node, path, line)      \
        ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,                             \
            "invalid XML attribute `%s' for element %s [@%s:%d]", attribute,   \
            node, path, line)

#define ERROR_REGISTER_NEGATIVE_DENSITY(material)                              \
        ERROR_VREGISTER(                                                       \
            PUMAS_RETURN_DENSITY_ERROR, "negative density for `%s'", material)

/* Function prototypes for the DCS implementations. */
/**
 * Handle for a DCS computation function.
 */
struct atomic_element;
typedef double(dcs_function_t)(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q);
/**
 * Handle for a polar angle sampling function.
 */
typedef double(polar_function_t)(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf);
/**
 * Generic doubly differential cross-section.
 */
typedef double ddcs_t(
    double Z, double A, double m, double K, double q, double Q2);

/* A collection of low level flags. */
/**
 * Node keys for the MDF files.
 */
enum mdf_key {
        /** An atomic component. */
        MDF_KEY_ATOMIC_COMPONENT,
        /** A composite material. */
        MDF_KEY_COMPOSITE,
        /** A component of a composite material. */
        MDF_KEY_COMPOSITE_COMPONENT,
        /** The root PUMAS node. */
        MDF_KEY_PUMAS,
        /** An atomic element. */
        MDF_KEY_ELEMENT,
        /** A base material. */
        MDF_KEY_MATERIAL,
        /** Any other node. */
        MDF_KEY_OTHER
};
/**
 * Nodes hierarchy in MDFs.
 */
enum mdf_depth {
        /** Outside of the root PUMAS node. */
        MDF_DEPTH_EXTERN = 0,
        /** Inside the root pumas node. */
        MDF_DEPTH_ROOT,
        /** Inside an element node. */
        MDF_DEPTH_ELEMENT,
        /** Inside a material node. */
        MDF_DEPTH_MATERIAL,
        /** Inside a composite material node. */
        MDF_DEPTH_COMPOSITE
};
/**
 * Tags for operations relative to the parsing of materials in MDFs.
 */
enum mdf_settings_operation {
        /** Free the names table. */
        MDF_INDEX_FREE = -1,
        /** Initialise the names table. */
        MDF_INDEX_INITIALISE,
        /** Add a new base material to the table. */
        MDF_INDEX_WRITE_MATERIAL,
        /** Finalise a base material info. */
        MDF_INDEX_FINALISE_MATERIAL,
        /** Add a new composite material to the table. */
        MDF_INDEX_INITIALISE_COMPOSITE,
        /** Update a composite material with a new component. */
        MDF_INDEX_UPDATE_COMPOSITE,
        /** Finalise the composite material. */
        MDF_INDEX_FINALISE_COMPOSITE
};

/* Low level data structures. */
/**
 * Temporary data for the computation of the Coulomb scattering.
 *
 * The DCS is given by a product of 3 Wentzel distributions with 1 atomic
 * screening parameter and 2 nuclear screening ones. A pole reduction allows
 * the analytical computation of the various momenta: total cross-section, first
 * transport path length, ...
 */
struct coulomb_data {
        /** The partial cross section for hard events. */
        double cs_hard;
        /** The normalisation of the macroscopic cross-section. */
        double normalisation;
        /** The number of screening parameters */
        int n_parameters;
        /** The amplitudes of the atomic screening terms. */
        double amplitude[3];
        /** The atomic and nuclear screening parameters. */
        double screening[4];
        /** The 1st order coefficients of the pole reduction of the DCS. */
        long double a[3];
        /** The 2nd order coefficients of the pole reduction of the DCS. */
        long double b[3];
        /** The nuclear coefficients of the pole reduction of the DCS. */
        long double c[4];
        /** The spin correction factor. */
        double fspin;
        /**
         * The parameters of the relativistic transform from Center of Mass (CM)
         * frame to the Laboratory one.
         */
        double fCM[2];
};
/**
 * Temporary data for the rendering of a Coulomb DEL. The event is randomised
 * using the inverse transform method. A root bracketing algorithm is used,
 * seeded with an approximate solution.
 */
struct coulomb_workspace {
        /** The index of the propagation material. */
        int material;
        /** The index of the hard scatterer element. */
        int ihard;
        /** The targeted cross section. */
        double cs_h;
        /** Placeholder for the table of Coulomb scattering data. */
        struct coulomb_data data[];
};
/**
 * Low level container for the local properties of a propagation medium.
 */
struct medium_locals {
        /** The public API properties exposed to the end user. */
        struct pumas_locals api;
        /** A flag telling if the material has a magnetic field or not. */
        int magnetized;
        /** The local's physics */
        const struct pumas_physics * physics;
};
/**
 *  Data for the default per context PRNG
 */
struct pumas_random_data {
/*
 * Version tag for the random data format. Increment whenever the
 * structure changes.
 */
#define RANDOM_BINARY_DUMP_TAG 0
        /** The initial seed */
        unsigned long seed;
        /** Index in the PRNG buffer */
        int index;
#define MT_PERIOD 624
        /** PRNG buffer (Mersenne Twister) */
        unsigned long buffer[MT_PERIOD];
};
/**
 * The local data managed by a simulation context.
 */
struct simulation_context {
        /** The public API settings exposed to the end user. */
        struct pumas_context api;
        /** Handle for Physics tables. */
        const struct pumas_physics * physics;
        /** Lifetime limit for the decay process. */
        double lifetime;
        /**
         * Last kinetic energy indices used in the tables.
         *
         * We keep a memory of the last indices accessed in tables. The memory
         * has a depth of 2 which should be a good compromise since most use
         * cases consider differences between an initial and final state.
         */
        int index_K_last[2];
        /** Last grammage indices used in the tables. */
        int index_X_last[2];
        /** Flag for the first step, for integration of various quantities. */
        int step_first;
        /** Tracking of stepping events. */
        enum pumas_event step_event;
        /** The expected next event during the stepping. */
        enum pumas_event step_foreseen;
        /** The kinetic limit converted to grammage. */
        double step_X_limit;
        /** The scaterring 1st transport path length of the previous step. */
        double step_invlb1;
        /** Data for the default PRNG. */
        struct pumas_random_data * random_data;
        /** Flag for the parity check of the Gaussian random generator.
         *
         * Gaussian variates are generated in pair using the Box-Muller
         * transform.
         */
        int randn_done;
        /** The next Gaussian variate. */
        double randn_next;
        /**
         * Pointer to the worspace for the temporary storage of intermediary
         * computations.
         */
        struct coulomb_workspace * workspace;
        /** Size of the user extended memory. */
        int extra_memory;
        /**
         * Placeholder for variable data storage with -double- memory alignment.
         *
         * Extra bytes are allocated for the workspace, the error stack and
         * any extended memory for end user usage.
         */
        long double data[];
};
/**
 * Handle for a stack of recorded frames.
 *
 * The frames are allocated in bunches. The bunches are managed as a chained
 * list.
 */
struct frame_stack {
        /** The memory size left, in bytes. */
        int size;
        /** Pointer to the next memory segment. */
        struct frame_stack * next;
        /** Pointer to the first frame in the stack. */
        struct pumas_frame * frame;
        /** Placeholder for frames */
        struct pumas_frame frames[];
};
/**
 * Low level container for a frame recorder.
 */
struct frame_recorder {
        /** The public API data exposed to the end user. */
        struct pumas_recorder api;
        /** Link to the last record. */
        struct pumas_frame * last;
        /** Link to the 1st entry of the chained list of stacks. */
        struct frame_stack * stack;
        /** Placeholder for extra data. */
        double data[];
};
/**
 * Data relative to an atomic element.
 */
struct atomic_element {
        /** The element atomic number. */
        double Z;
        /** The element atomic mass, in g/mol. */
        double A;
        /** The element Mean Excitation Energy (MEE), in eV. */
        double I;
        /** The element tabulation index */
        int index;
        /** The element name. */
        char * name;
        /** Placeholder for user data with -double- memory alignment. */
        double data[];
};
/**
 * An atomic component of a material.
 */
struct material_component {
        /** The atomic element index. */
        int element;
        /** The element mass fraction in the material. */
        double fraction;
};
/**
 * A component of a composite material.
 */
struct composite_component {
        /** The constituent base material index. */
        int material;
        /** The component mass fraction in the composite. */
        double fraction;
};
/**
 * Handle for a composite material.
 */
struct composite_material {
        /** The number of sub components. */
        int n_components;
        /** Placeholder for the sub components' data. */
        struct composite_component component[];
};
/**
 * Temporary data for the parsing of a MDF.
 */
struct mdf_buffer {
        /** Flag for the dry initialisation mode. */
        int dry_mode;
        /** Handle to the MDF. */
        FILE * fid;
        /** Current position in the read buffer. */
        char * pos;
        /** The number of bytes left to be read. */
        int left;
        /** The total size of the read buffer. */
        int size;
        /** The current line number in the MDF file. */
        int line;
        /** Current node depth during the MDF parsing. */
        enum mdf_depth depth;
        /** Counter for the number of base materials in a composite. */
        int materials_in;
        /** Counter for the number of elements in a material. */
        int elements_in;
        /** Pointer to the current MDF. */
        const char * mdf_path;
        /** The number of kinetic energy rows in a dE/dX file. */
        int n_energies;
        /** The total number of materials, base and composites. */
        int n_materials;
        /** The number of composite materials. */
        int n_composites;
        /** The number of atomic elements. */
        int n_elements;
        /** The header length of dE/dX files. */
        int n_energy_loss_header;
        /** The total number of atomic element components. */
        int n_components;
        /** The maximum number of atomic elements in a single material. */
        int max_components;
        /** The total byte size for the storage of composite materials. */
        int size_composite;
        /** The total size of the path to the dE/dX files. */
        int size_dedx_path;
        /** The total size of elements names. */
        int size_elements_names;
        /** The total size of materials names. */
        int size_materials_names;
        /** Placeholder for the read buffer. */
        char data[];
};
/*!
 * Pointers to the data fields of a node in a MDF file.
 *
 * The pointers refer to an mdf_buffer object. If the buffer is refilled the
 * links are no mode valid.
 */
struct mdf_node {
        /** The node key. */
        enum mdf_key key;
        /** Flag telling if this is a head node. */
        int head;
        /** Flag telling if this is a tail node. */
        int tail;
        /** The first attribute names. */
        union attribute1 {
                /** The node name. */
                char * name;
        } at1;
        /** The second attribute names. */
        union attribute2 {
                /** The energy loss file name. */
                char * file;
                /** The atomic number. */
                char * Z;
                /** The mass fraction. */
                char * fraction;
        } at2;
        /** The third attribute names. */
        union attribute3 {
                /** The atomic mass. */
                char * A;
                /** The composite component's density. */
                char * density;
        } at3;
        /** The fourth attribute names. */
        union attribute4 {
                /** The mean excitation energy (MEE). */
                char * I;
        } at4;
};
/**
 * Temporary data for a DEL event.
 */
struct del_info {
        union {
                /** The energy transfer. */
                double Q;
                /** The Monte-Carlo weight factor. */
                double weight;
        }
        /** Data specific to reverse Monte-Carlo. */
        reverse;
        /** The index of the inelastic sub-process. */
        int process;
        /** The index of the target element. */
        int element;
};
/**
 * Global data shared by all simulation contexts.
 */
struct pumas_physics {
/*
 * Version tag for the physics data format. Increment whenever the
 * structure changes.
 */
#define PHYSICS_BINARY_DUMP_TAG 13

        /** The total byte size of the shared data. */
        int size;
        /** The number of kinetic energy values in the dE/dX tables. */
        int n_energies;
        /** The total number of materials, basic and composites. */
        int n_materials;
        /** The total number of composite materials. */
        int n_composites;
        /** The total number of declared atomic_elements. */
        int n_elements;
        /** The total number of atomic components in materials. */
        int n_components;
        /** The maximum number of atomic components in a single material. */
        int max_components;
        /** The number of header lines in a dE/dX table. */
        int n_energy_loss_header;
        /**
         * Offset for the tabulation of DCS and their polynomial
         * approximations.
         */
        int dcs_model_offset;
        /** The number of tabulated dcs_values */
        int n_table_dcs;
        /** The transported particle type. */
        enum pumas_particle particle;
        /** The transported particle decay length, in m. */
        double ctau;
        /** The transported particle rest mass, in GeV. */
        double mass;
        /** The relative cutoff between CEL and DELs. */
        double cutoff;
        /** Ratio of EHS path length w.r.t. the first transport path length. */
        double elastic_ratio;
        /** Path to the current MDF. */
        char * mdf_path;
        /** Path where the dE/dX files are stored. */
        char * dedx_path;
        /** Names of the dE/dX files. */
        char ** dedx_filename;
        /** The tabulated values of the kinetic energy. */
        double * table_K;
        double * table_K_dX;
        double * table_K_dNI_in;
        double * table_K_dNI_el;
        /** The tabulated values of the total grammage (CSDA range). */
        double * table_X;
        double * table_X_dK;
        double * table_X_dT;
        /** The tabulated values of the total proper time. */
        double * table_T;
        double * table_T_dK;
        /** The tabulated values of the average energy loss. */
        double * table_dE;
        double * table_dE_dK;
        /** The tabulated values of the energy straggling. */
        double * table_Omega;
        double * table_Omega_dK;
        /** The tabulated values of the EHS number of interaction lengths. */
        double * table_NI_el;
        double * table_NI_el_dK;
        /**
         * The tabulated values of the number of interaction lengths for
         * inelastic DELs.
         */
        double * table_NI_in;
        double * table_NI_in_dK;
        /** The tabulated cross section values. */
        double * table_CS;
        double * table_CS_dK;
        /** The tabulated cross section fractions. */
        double * table_CSf;
        double * table_CSf_dK;
        /** The tabulated cross section normalisation. */
        double * table_CSn;
        double * table_CSn_dK;
        /* The tabulated data for DCSs **/
        float * table_DCS;
        float * table_DCS_x;
        float * table_DCS_envelope;
        /** The element wise fractional threshold for DELs. */
        double * table_Xt;
        /** The total kinetic threshold for DELs. */
        double * table_Kt;
        /**
         * The tabulated values of the magnetic deflection momenta, within
         * the CSDA.
         */
        double * table_Li;
        double * table_Li_dK;
        /** The last tabulated value of the ionisation energy loss. */
        double * table_a_max;
        /** The last tabulated value of the radiative energy loss. */
        double * table_b_max;
        /**
         * The tabulated angular cutoff values for the splitting of Coulomb
         * scattering.
         */
        double * table_Mu0;
        double * table_Mu0_dK;
        /** The tabulated interaction lengths for DEL Coulomb events. */
        double * table_Lb;
        double * table_Lb_dK;
        /** The tabulated multiple scattering 1st moment. */
        double * table_Ms1;
        double * table_Ms1_dK;
        /** The number of elements in a material. */
        int * elements_in;
        /** The reference density of a material. */
        double * material_density;
        /** The relative electronic density of a material. */
        double * material_ZoA;
        /** The mean excitation energy of a base material. */
        double * material_I;
        /** The Sternheimer scaling ratio of a material. */
        double * material_aS;
        /** The properties of an atomic element . */
        struct atomic_element ** element;
        /** The composition of a base material. */
        struct material_component ** composition;
        /** The composition of a composite material. */
        struct composite_material ** composite;
        /** The material names. */
        char ** material_name;
        /** The Bremsstrahlung model. */
        char * model_bremsstrahlung;
        /** The pair_production model. */
        char * model_pair_production;
        /** The photonuclear model. */
        char * model_photonuclear;
        /** The Bremsstrahlung DCS. */
        pumas_dcs_t * dcs_bremsstrahlung;
        /** The pair_production DCS. */
        pumas_dcs_t * dcs_pair_production;
        /** The photonuclear DCS. */
        pumas_dcs_t * dcs_photonuclear;
        /**
         * Placeholder for shared data storage with -double- memory alignment.
         */
        double data[];
};

struct error_context {
        enum pumas_return code;
        pumas_function_t * function;
#define ERROR_MSG_LENGTH 1024
        char message[ERROR_MSG_LENGTH];
};

/**
 * Handle for an atomic element within a material.
 *
 * This structure is a proxy exposing some data of an atomic element within
 * the material last processed by `pumas_tabulation_tabulate`.
 */
struct physics_element {
        /** Linked list pointer to the previous element. */
        struct physics_element * prev;
        /** Linked list pointer to the next element. */
        struct physics_element * next;
        /** The element index. */
        int index;
        /** The mass fraction of the element in the current material. */
        double fraction;
};

/**
 * Handle for tabulation data.
 *
 * This structure gathers data related to the tabulation of the energy loss of
 * materials with the `physics_tabulate` function. **Note** that the two last
 * parameters: *path* and *elements* should not be set directly. They are filled
 * in (updated) by the `physics_tabulate` function. Note also that if no energy
 * grid is provided then a default one is set.
 *
 * **Warning**: the energy grid should not be changed between successive calls
 * to `physics_tabulate`. If a new energy grid is needed then a new
 * `physics_tabulation_data` object must be created.
 */
struct physics_tabulation_data {
        /** The number of kinetic energy values to tabulate. Providing a value
         * of zero or less results in a default energy grid being set.
         */
        int n_energies;
        /** Array of kinetic energy values to tabulate. Providing a `NULL`
         * value results in a default energy grid being set.
         */
        double * energy;
        /** Flag to enable overwriting an existing energy loss file. */
        int overwrite;
        /** Path to a directory where the tabulation should be written. */
        char * outdir;
        /** Index of the material to tabulate */
        int material;
        /** Path to the energy loss file of the last tabulated material. */
        char * path;
        /** List of atomic elements contained in the tabulated material(s). */
        struct physics_element * elements;
        /** Temporary work data */
        double * work;
};

/**
 * Default error handler.
 */
static void default_error_handler(
    enum pumas_return rc, pumas_function_t * caller, const char * message)
{
        /* Dump the error summary */
        fputs("pumas: library error. See details below\n", stderr);
        fprintf(stderr, "error: %s\n", message);

        /* Exit to the OS */
        exit(EXIT_FAILURE);
}

/**
 * Shared data for the error handling.
 */
static struct {
        pumas_handler_cb * handler;
        int catch;
        struct error_context catch_error;
} s_error = { &default_error_handler, 0 };

/* Prototypes of low level static functions. */
/**
 * Encapsulations of the tabulated CEL and DEL properties.
 */
static double cel_grammage(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic);
static double cel_grammage_as_time(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double time);
static double cel_proper_time(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic);
static double cel_kinetic_energy(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double grammage);
static double cel_energy_loss(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic);
static double cel_straggling(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic);
static double cel_magnetic_rotation(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic);
static double del_cross_section(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic);
static double del_interaction_length(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic);
static double del_kinetic_from_interaction_length(
    const struct pumas_physics * physics, struct pumas_context * context,
    int material, double nI);
static double ehs_interaction_length(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic);
static double ehs_kinetic_from_interaction_length(
    const struct pumas_physics * physics, struct pumas_context * context,
    enum pumas_mode scheme, int material, double nI);
/**
 * Routines related to DCS: implementation and handling.
 */
static inline dcs_function_t * dcs_get(int process);
static inline int dcs_get_index(dcs_function_t * dcs_func);
static inline void dcs_get_range(int process, double Z, double mass,
    double energy, double * qmin, double * qmax);
static double dcs_bremsstrahlung(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q);
static double dcs_bremsstrahlung_transport(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double cutoff);
static double dcs_pair_production(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q);
static double dcs_pair_production_transport(
    const struct pumas_physics * physics, const struct atomic_element * element,
    double K, double cutoff);
static inline void dcs_pair_production_range(
    double Z, double m, double K, double * qmin, double * qmax);
static double dcs_photonuclear(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q);
static inline void dcs_photonuclear_range(
    double m, double K, double * qmin, double * qmax);
static inline int dcs_photonuclear_check(double m, double K, double q);
static inline ddcs_t * dcs_photonuclear_ddcs(
    const struct pumas_physics * physics, pumas_dcs_t ** dcs);
static double dcs_photonuclear_transport(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double cutoff);
static double dcs_ionisation(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q);
static double dcs_ionisation_integrate(const struct pumas_physics * physics,
    int mode, const struct atomic_element * element, double K, double xlow,
    double xhigh);
static double dcs_ionisation_randomise(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double K, double xlow);
static double dcs_evaluate(const struct pumas_physics * physics,
    struct pumas_context * context, dcs_function_t * dcs_func,
    const struct atomic_element * element, double K, double q);

/**
 * Implementations of polar angle distributions and accessor.
 */
static inline polar_function_t * polar_get(int process);
static double polar_bremsstrahlung(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf);
static double polar_pair_production(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf);
static double polar_photonuclear(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf);
static double polar_ionisation(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf);
/**
 * Low level routines for the propagation in matter.
 */
static enum pumas_event transport_with_csda(
    const struct pumas_physics * physics, struct pumas_context * context,
    struct pumas_state * state, struct pumas_medium * medium,
    struct medium_locals * locals, struct error_context * error_);
static enum pumas_return transport_csda_deflect(
    const struct pumas_physics * physics, struct pumas_context * context,
    struct pumas_state * state, struct pumas_medium * medium,
    struct medium_locals * locals, double ki, double distance,
    struct error_context * error_);
static enum pumas_return csda_magnetic_transport(
    const struct pumas_physics * physics, struct pumas_context * context,
    int material, double density, double magnet, double charge, double kinetic,
    double phase, double * x, double * y, double * z,
    struct error_context * error_);
static enum pumas_event transport_with_stepping(
    const struct pumas_physics * physics, struct pumas_context * context,
    struct pumas_state * state, struct pumas_medium ** medium_ptr,
    struct medium_locals * locals, double step_max_medium,
    enum pumas_step step_max_type, double step_max_locals,
    struct error_context * error_);
static double transport_set_locals(const struct pumas_context * context,
    struct pumas_medium * medium, struct pumas_state * state,
    struct medium_locals * locals);
static void transport_limit(const struct pumas_physics * physics,
    struct pumas_context * context, const struct pumas_state * state,
    int material, double di, double Xi, double * distance_max);
static void transport_do_del(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material);
static void transport_do_ehs(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material);
/**
 * Low level routines for randomising DELs.
 */
static polar_function_t * del_randomise_forward(
    const struct pumas_physics * physics, struct pumas_context * context,
    struct pumas_state * state, int material, int * process,
    const struct atomic_element ** element);
static polar_function_t * del_randomise_reverse(
    const struct pumas_physics * physics, struct pumas_context * context,
    struct pumas_state * state, int material, int * process,
    const struct atomic_element ** element);
static void del_randomise_power_law(struct pumas_context * context,
    double alpha, double xmin, double xmax, double * p_r, double * p_w);
static void del_randomise_target(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material,
    struct del_info * info);
/**
 * Helper routine for recording a state.
 */
static void record_state(struct pumas_context * context,
    struct pumas_medium * medium, enum pumas_event event,
    struct pumas_state * state);
/**
 * For memory padding.
 */
static int memory_padded_size(int size, int pad_size);
/**
 * For error handling.
 */
static enum pumas_return error_raise(struct error_context * context);
static enum pumas_return error_format(struct error_context * context,
    enum pumas_return rc, const char * file, int line, const char * format,
    ...);
/**
 * Routines for the Coulomb scattering and Transverse Transport (TT).
 */
static void coulomb_screening_parameters(double Z, double A, double m,
    double kinetic, double kinetic0, int * n_parameters, double * amplitude,
    double * screening);
static double coulomb_nuclear_form_factor(double mu, double N);
static double coulomb_normalisation(double Z, double A, double mass,
    double kinetic, double kinetic0);
static double coulomb_ehs_length(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic);
static double coulomb_spin_factor(double mass, double kinetic);
static void coulomb_frame_parameters(double Z, double A, double mass,
    double kinetic, double * kinetic0, double * parameters);
static void coulomb_pole_reduction(int n_parameters,
    const double * amplitude, const double * screening, long double * a,
    long double * b, long double * c);
static double coulomb_restricted_cs(double mu0, double fspin,
    int n_parameters, const double * screening, const long double * a,
    const long double * b, const long double * c);
static void coulomb_transport_coefficients(double mu, double fspin,
    int n_parameters, const double * screening, const long double * a,
    const long double * b, const long double * c, double * coefficient);
static double transverse_transport_electronic(
    double ZoA, double I, double aS, double mass, double kinetic, double nu);
/**
 * Routines for handling tables: interpolation and utility accessors.
 */
static void table_bracket(
    const double * table, double value, int * p1, int * p2);
static int table_index(const struct pumas_physics * physics,
    struct pumas_context * context, const double * table, double value);
static double table_interpolate_pchip(const struct pumas_physics * physics,
    struct pumas_context * context, const double * table_X,
    const double * table_Y, const double * table_M, double x);
static void table_get_msc(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic, double * mu0,
    double * invlb1);
static inline double * table_get_K(
    const struct pumas_physics * physics, int row);
static inline double * table_get_K_dX(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_K_dNI_el(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_K_dNI_in(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_X(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_X_dK(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_X_dT(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_T(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_T_dK(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_dE(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_dE_dK(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_Omega(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_Omega_dK(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_NI_el(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_NI_el_dK(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_NI_in(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_NI_in_dK(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_CS(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_CS_dK(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_CSf(
    const struct pumas_physics * physics, int process, int component, int row);
static inline double * table_get_CSf_dK(
    const struct pumas_physics * physics, int process, int component, int row);
static inline double * table_get_CSn(
    const struct pumas_physics * physics, int process, int element, int row);
static inline double * table_get_CSn_dK(
    const struct pumas_physics * physics, int process, int element, int row);
static inline double * table_get_Xt(
    const struct pumas_physics * physics, int process, int element, int row);
static inline double * table_get_Kt(
    const struct pumas_physics * physics, int material);
static inline double * table_get_cel(const struct pumas_physics * physics,
    int process, int element, int row, double * table);
static inline double * table_get_stg(const struct pumas_physics * physics,
    int process, int element, int row, double * table);
static inline double * table_get_Li(
    const struct pumas_physics * physics, int material, int order, int row);
static inline double * table_get_Li_dK(
    const struct pumas_physics * physics, int material, int order, int row);
static inline double * table_get_a_max(
    const struct pumas_physics * physics, int material);
static inline double * table_get_b_max(
    const struct pumas_physics * physics, int scheme, int material);
static inline double * table_get_Mu0(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_Mu0_dK(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_Lb(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_Lb_dK(
    const struct pumas_physics * physics, int material, int row);
static inline double * table_get_Ms1(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_Ms1_dK(
    const struct pumas_physics * physics, int scheme, int material, int row);
static inline double * table_get_ms1(const struct pumas_physics * physics,
    int scheme, int element, int row, double * table);
static inline float * table_get_dcs(const struct pumas_physics * physics,
    int process, int element, int kinetic);
static inline float * table_get_dcs_envelope(
    const struct pumas_physics * physics, int process, int element,
    int kinetic);
/**
 * Routine(s) wrapping static data
 */
static double data_nuclear_radius(double Z, double A);
/**
 * Low level routines for the stepping.
 */
static enum pumas_return step_transport(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int straight,
    struct pumas_medium * medium, struct medium_locals * locals,
    double grammage_max, double step_max_medium, enum pumas_step step_max_type,
    double * step_max_locals, struct pumas_medium ** out_medium,
    struct error_context * error_);
static void step_fluctuate(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material,
    double Xtot, double dX, double * kf, double * dE);
static double step_randn(struct pumas_context * context);
static void step_rotate_direction(struct pumas_context * context,
    struct pumas_state * state, double mu);
/**
 * I/O utility routines.
 */
static enum pumas_return io_parse_dedx_file(struct pumas_physics * physics,
    FILE * fid, int material, const char * filename,
    struct error_context * error_);
static enum pumas_return io_parse_dedx_row(struct pumas_physics * physics,
    char * buffer, int material, int * row, const char * filename, int line,
    struct error_context * error_);
static enum pumas_return io_read_line(FILE * fid, char ** buffer,
    const char * filename, int line, struct error_context * error_);
/**
 * Routines for the parsing of MDFs.
 */
static enum pumas_return mdf_parse_settings(
    const struct pumas_physics * physics, struct mdf_buffer * mdf,
    const char * dedx_path, struct error_context * error_);
static int mdf_settings_index(
    int operation, int value, struct error_context * error_);
static int mdf_settings_name(
    int size, char prefix, const char * name, struct error_context * error_);
static enum pumas_return mdf_parse_kinetic(struct mdf_buffer * mdf,
    const char * path, int n_energies, double * energy,
    struct error_context * error_);
static enum pumas_return mdf_parse_elements(
    const struct pumas_physics * physics, struct mdf_buffer * mdf,
    struct error_context * error_);
static enum pumas_return mdf_parse_materials(struct pumas_physics * physics,
    struct mdf_buffer * mdf, struct error_context * error_);
static enum pumas_return mdf_parse_composites(struct pumas_physics * physics,
    struct mdf_buffer * mdf, struct error_context * error_);
static enum pumas_return mdf_get_node(struct mdf_buffer * mdf,
    struct mdf_node * node, struct error_context * error_);
static enum pumas_return mdf_skip_pattern(struct mdf_buffer * mdf,
    const char * pattern, struct error_context * error_);
static enum pumas_return mdf_format_path(const char * directory,
    const char * mdf_path, char ** filename, int * offset_dir, int * size_name,
    struct error_context * error_);
/**
 * Routines for the pre-computation of various properties: CEL, DCS, ...
 */
static enum pumas_return compute_composite(struct pumas_physics * physics,
    int material, struct error_context * error_);
static enum pumas_return compute_composite_density(
    struct pumas_physics * physics, int material,
    struct error_context * error_);
static void compute_composite_weights(
    struct pumas_physics * physics, int material);
static void compute_composite_tables(
    struct pumas_physics * physics, int material);
static void compute_cel_integrals(struct pumas_physics * physics, int imed);
static enum pumas_return compute_scattering(struct pumas_physics * physics,
    int imed, struct error_context * error_);
static void compute_kinetic_integral(
    struct pumas_physics * physics, double * table, double * work);
static void compute_time_integrals(
    struct pumas_physics * physics, int material);
static void compute_cel_grammage_integral(
    struct pumas_physics * physics, int scheme, int material);
static void compute_pchip_coeffs(struct pumas_physics * physics, int material);
static void compute_pchip_elements_coeffs(struct pumas_physics * physics);
static void compute_pchip_integral_coeffs(
    struct pumas_physics * physics, int material);
static void compute_pchip_scattering_coeffs(
    struct pumas_physics * physics, int material);
static void compute_pchip_scattering_integral_coeffs(
    struct pumas_physics * physics, int material);
static void compute_csda_magnetic_transport(
    struct pumas_physics * physics, int imed);
static enum pumas_return compute_scattering_parameters(
    struct pumas_physics * physics, int medium_index, int row,
    struct error_context * error_);
static enum pumas_return compute_msc_soft(struct pumas_physics * physics,
    int row, double ** data, struct error_context * error_);
static double compute_msc_electronic(struct pumas_physics * physics,
    enum pumas_mode mode, int material, int row);
static double compute_cutoff_objective(
    const struct pumas_physics * physics, double mu, void * workspace);
static double * compute_cel_and_del(struct pumas_physics * physics, int row);
static void compute_regularise_del(
    struct pumas_physics * physics, int material);
static double compute_dcs_integral(struct pumas_physics * physics, int mode,
    const struct atomic_element * element, double kinetic, dcs_function_t * dcs,
    double xlow, double xhigh, int nint);
static void compute_ZoA(struct pumas_physics * physics, int material);
static void compute_MEE(struct pumas_physics * physics, int material);
static enum pumas_return compute_dcs_table(
    struct pumas_physics * physics, int element, struct error_context * error_);
static enum pumas_return physics_tabulate(struct pumas_physics * physics,
    struct physics_tabulation_data * data, struct error_context * error_);
static void physics_tabulation_clear(const struct pumas_physics * physics,
    struct physics_tabulation_data * data);
static struct physics_element * tabulation_element_create(
    struct physics_tabulation_data * data, int element);
static struct physics_element * tabulation_element_get(
    struct physics_tabulation_data * data, int element);
static struct atomic_shell * atomic_shell_create(
    const struct pumas_physics * physics, int material, int * n_shells_ptr,
    double * aS_ptr);
/**
 * Helper function for mapping an atomic element from its name.
 */
static int element_index(
    const struct pumas_physics * physics, const char * name);
/**
 * Helper function for mapping a material from its name.
 */
static enum pumas_return material_index(const struct pumas_physics * physics,
    const char * material, int * index, struct error_context * error_);
/**
 * Various math utilities, for integration, root finding and SVD.
 */
static int math_find_root(
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xb,
    const double * fa_p, const double * fb_p, double xtol, double rtol,
    int iter, void * params, double * x0);
static int math_find_minimum(int algo,
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xc,
    const double * fa_p, const double * fc_p, double tol, int max_iter,
    void * params, double * x0, double * f0);
static int math_find_minimum_brent(
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xb, double xc,
    const double * fb_p, double tol, int max_iter, void * params, double * x0,
    double * f0);
static int math_find_minimum_bisection(
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xb, double xc,
    const double * fb_p, double tol, int max_iter, void * params, double * x0,
    double * f0);
static int math_gauss_quad(int n, double * p1, double * p2);
static void math_gauss_quad_coefficients(
    int n, const double ** xGQ, const double ** wGQ);
static void math_gauss_quad_initialise(
   int n, double xmin, double xmax, int log, double * xGQ, double * wGQ);
static double math_diff3(
    double x0, double x1, double x2, double y1, double y2, double y3);
static void math_pchip_initialise(
    int der, int n, const double * x, const double * y, double * m);
static void math_pchip_integrate(
    int n, const double * x, double * y, double * d);
static inline double math_pchip_interpolate(
    double t, double y0, double y1, double m0, double m1);

/* Below is the implementation of the public API functions. See pumas.h for a
 * detailed description of each function.
 */
#if (GDB_MODE)
/**
 * A flag for floating point exceptions.
 */
static int fe_initialised = 0;
static int fe_status;
#endif

/* Getter for library constants */
PUMAS_API enum pumas_return pumas_constant(
    enum pumas_constant index, double * value)
{
        ERROR_INITIALISE(pumas_constant);

        const double values[] = {ALPHA_EM, AVOGADRO_NUMBER, BOHR_RADIUS,
            ELECTRON_MASS, ELECTRON_RADIUS, HBAR_C, MUON_C_TAU, MUON_MASS,
            NEUTRON_MASS, PION_MASS, PROTON_MASS, TAU_C_TAU, TAU_MASS};

        if (value == NULL) {
                return ERROR_MESSAGE(PUMAS_RETURN_VALUE_ERROR,
                    "NULL value pointer");
        }

        if ((index < 0) || (index >= PUMAS_N_CONSTANTS)) {
                *value = 0;
                return ERROR_FORMAT(PUMAS_RETURN_INDEX_ERROR,
                    "invalid `constant' index [%d]", index);
        }

        *value = values[index];

        return PUMAS_RETURN_SUCCESS;
}

/*
 * Public library functions: user supplied memory allocation.
 */
static pumas_allocate_cb * allocate = malloc;
static pumas_reallocate_cb * reallocate = realloc;
static pumas_deallocate_cb * deallocate = free;

/**
 * Set the memory allocation function for the PUMAS library.
 */
void pumas_memory_allocator(pumas_allocate_cb * allocator)
{
        allocate = (allocator == NULL) ? malloc : allocator;
}

/**
 * Set the memory reallocation function for the PUMAS library.
 */
void pumas_memory_reallocator(pumas_reallocate_cb * reallocator)
{
        reallocate = (reallocator == NULL) ? realloc : reallocator;
}

/**
 * Set the memory deallocation function for the PUMAS library.
 */
void pumas_memory_deallocator(pumas_deallocate_cb * deallocator)
{
        deallocate = (deallocator == NULL) ? free : deallocator;
}

/*
 * Public library functions: initialisation and termination.
 */

/* Routine for checking the validity of a DCS model name (forward decl.) */
static enum pumas_return dcs_check_model(enum pumas_process process,
     const char * model, struct error_context * error_);

/*
 * Low level initialisation. If *dry_mode* is not null the energy loss tables
 * are not loaded and processed.
 */
static enum pumas_return _initialise(struct pumas_physics ** physics_ptr,
    enum pumas_particle particle, const char * mdf_path, const char * dedx_path,
    int dry_mode, const struct pumas_physics_settings * settings_)
{
        ERROR_INITIALISE(pumas_physics_create);

        /* Check if the Physics pointer is NULL. */
        if (physics_ptr == NULL) {
                return ERROR_NULL_PHYSICS();
        }
        *physics_ptr = NULL;
#if (GDB_MODE)
        /* Save the floating points exceptions status and enable them. */
        if (!fe_initialised) {
                fe_status = fegetexcept();
                feclearexcept(FE_ALL_EXCEPT);
                feenableexcept(
                    FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW);
                fe_initialised = 1;
        }
#endif
        FILE * fid_mdf = NULL;
        struct mdf_buffer * mdf = NULL;
        const int pad_size = sizeof(*((*physics_ptr)->data));
#define N_DATA_POINTERS 53
        int size_data[N_DATA_POINTERS];

        /* Check the particle type. */
        if ((particle != PUMAS_PARTICLE_MUON) &&
            (particle != PUMAS_PARTICLE_TAU)) {
                return ERROR_FORMAT(PUMAS_RETURN_UNKNOWN_PARTICLE,
                    "invalid particle index `%d'", (int)particle);
        }

        /* Check and unpack any extra settings */
        struct pumas_physics_settings opts = {
                DEFAULT_CUTOFF,
                DEFAULT_ELASTIC_RATIO,
                DEFAULT_BREMSSTRAHLUNG,
                DEFAULT_PAIR_PRODUCTION,
                DEFAULT_PHOTONUCLEAR
        };
        if (settings_ != NULL) {
                if (settings_->cutoff >= 1) {
                        return ERROR_FORMAT(PUMAS_RETURN_VALUE_ERROR,
                            "bad cutoff value for energy losses (expected a "
                            "value in ]0, 1[,  got %g)",
                            settings_->cutoff);
                } else if (settings_->cutoff > 0) {
                        opts.cutoff = settings_->cutoff;
                }

                if (settings_->elastic_ratio >= 1) {
                        return ERROR_FORMAT(PUMAS_RETURN_VALUE_ERROR,
                            "bad ratio value for elastic scattering "
                            "(expected a value in ]0, 1[,  got %g)",
                            settings_->elastic_ratio);
                } else if (settings_->elastic_ratio > 0) {
                        opts.elastic_ratio = settings_->elastic_ratio;
                }

                if (settings_->bremsstrahlung != NULL) {
                        if (dcs_check_model(PUMAS_PROCESS_BREMSSTRAHLUNG,
                            settings_->bremsstrahlung, error_) ==
                            PUMAS_RETURN_SUCCESS) {
                                opts.bremsstrahlung =
                                    settings_->bremsstrahlung;
                        } else {
                                return ERROR_RAISE();
                        }
                }

                if (settings_->pair_production != NULL) {
                        if (dcs_check_model(PUMAS_PROCESS_PAIR_PRODUCTION,
                            settings_->pair_production, error_) ==
                            PUMAS_RETURN_SUCCESS) {
                                opts.pair_production =
                                    settings_->pair_production;
                        } else {
                                return ERROR_RAISE();
                        }
                }

                if (settings_->photonuclear != NULL) {
                        if (dcs_check_model(PUMAS_PROCESS_PHOTONUCLEAR,
                            settings_->photonuclear, error_) ==
                            PUMAS_RETURN_SUCCESS) {
                                opts.photonuclear =
                                    settings_->photonuclear;
                        } else {
                                return ERROR_RAISE();
                        }
                }
        }

        /* Check the path to energy loss tables. */
        struct pumas_physics * physics = NULL;
        if (dedx_path == NULL) dedx_path = getenv("PUMAS_DEDX");
        if (dedx_path == NULL) dedx_path = "@.";

        /* Parse the MDF. */
        const int size_mdf = 2048;
        const char * file_mdf =
            (mdf_path != NULL) ? mdf_path : getenv("PUMAS_MDF");
        if (file_mdf == NULL) {
                ERROR_REGISTER(PUMAS_RETURN_UNDEFINED_MDF,
                    "missing materials description file");
                goto clean_and_exit;
        }
        int size_path = strlen(file_mdf) + 1;
        fid_mdf = fopen(file_mdf, "r");
        if (fid_mdf == NULL) {
                ERROR_VREGISTER(PUMAS_RETURN_PATH_ERROR,
                    "could not open MDF file `%s'", mdf_path);
                goto clean_and_exit;
        }
        mdf = allocate(size_mdf);
        if (mdf == NULL) {
                ERROR_REGISTER_MEMORY();
                goto clean_and_exit;
        }
        mdf->dry_mode = dry_mode;
        mdf->mdf_path = file_mdf;
        mdf->fid = fid_mdf;
        mdf->size = size_mdf - sizeof(*mdf);
        if ((mdf_parse_settings(*physics_ptr, mdf, dedx_path, error_)) !=
            PUMAS_RETURN_SUCCESS)
                goto clean_and_exit;

        /* Backup the parsed settings. */
        struct mdf_buffer settings;
        memcpy(&settings, mdf, sizeof(settings));

        /* Compute the size of the dcs binning */
        int n_table_dcs = DCS_MODEL_N_REVERSE;
        if (opts.cutoff < DCS_MODEL_X_REVERSE) {
                int n = (int)(log10(DCS_MODEL_X_REVERSE / opts.cutoff) *
                    DCS_MODEL_PER_DECADE);
                if (n < DCS_MODEL_N_MIN) n = DCS_MODEL_N_MIN;
                n_table_dcs += n;
        }

        /* Compute the memory mapping. */
        int imem = 0;
        /* mdf_path. */
        size_data[imem++] =
            memory_padded_size(sizeof(char) * size_path, pad_size);
        /* dedx_path. */
        size_data[imem++] = memory_padded_size(
            sizeof(char) * settings.size_dedx_path, pad_size);
        /* dedx_filename. */
        size_data[imem++] = memory_padded_size(
            sizeof(char *) * (settings.n_materials - settings.n_composites),
            pad_size);
        /* table_K. */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_energies, pad_size);
        /* table_K_dX. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_K_dNI_in. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_K_dNI_el. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
            settings.n_materials * settings.n_energies, pad_size);
        /* table_X. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_X_dK. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_X_dT. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_T. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_T_dK. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_dE. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_dE_dK. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
                settings.n_materials * settings.n_energies,
            pad_size);
        /* table_Omega. */
        size_data[imem++] = memory_padded_size(sizeof(double) *
            settings.n_materials * settings.n_energies, pad_size);
        /* table_Omega_dK. */
        size_data[imem++] = memory_padded_size(sizeof(double) *
            settings.n_materials * settings.n_energies, pad_size);
        /* table_NI_el. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
            settings.n_materials * settings.n_energies, pad_size);
        /* table_NI_el_dK. */
        size_data[imem++] = memory_padded_size(sizeof(double) * N_SCHEMES *
            settings.n_materials * settings.n_energies, pad_size);
        /* table_NI_in. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_NI_in_dK. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_CS. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_CS_dK. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_CSf. */
        size_data[imem++] = memory_padded_size(sizeof(double) *
                N_DEL_PROCESSES * settings.n_components * settings.n_energies,
            pad_size);
        /* table_CSf_dK. */
        size_data[imem++] = memory_padded_size(sizeof(double) *
                N_DEL_PROCESSES * settings.n_components * settings.n_energies,
            pad_size);
        /* table_CSn. */
        size_data[imem++] = memory_padded_size(sizeof(double) *
                N_DEL_PROCESSES * settings.n_elements * settings.n_energies,
            pad_size);
        /* table_CSn_dK. */
        size_data[imem++] = memory_padded_size(sizeof(double) *
                N_DEL_PROCESSES * settings.n_elements * settings.n_energies,
            pad_size);
        /* table_DCS. */
        size_data[imem++] = memory_padded_size(sizeof(float) *
            (N_DEL_PROCESSES - 1) * settings.n_elements * settings.n_energies *
            2 * n_table_dcs, pad_size);
        /* table_DCS_x. */
        size_data[imem++] = memory_padded_size(
            n_table_dcs * sizeof(float), pad_size);
        /* table_DCS_envelope. */
        size_data[imem++] = memory_padded_size(sizeof(float) *
            (N_DEL_PROCESSES - 1) * settings.n_elements * settings.n_energies *
            2, pad_size);
        /* table_Xt */
        size_data[imem++] = memory_padded_size(sizeof(double) *
                N_DEL_PROCESSES * settings.n_elements * settings.n_energies,
            pad_size);
        /* table_Kt. */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials, pad_size);
        /* table_Li. */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials *
                    (N_LARMOR_ORDERS + 1) * settings.n_energies,
                pad_size);
        /* table_Li_dK. */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials *
                    (N_LARMOR_ORDERS + 1) * settings.n_energies,
                pad_size);
        /* table_a_max. */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials, pad_size);
        /* table_b_max. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * N_SCHEMES * settings.n_materials, pad_size);
        /* table_Mu0. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_Mu0_dK. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_Lb. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_Lb_dK. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * settings.n_materials * settings.n_energies,
            pad_size);
        /* table_Ms1. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * (N_SCHEMES + 1) * settings.n_materials *
            settings.n_energies, pad_size);
        /* table_Ms1_dK. */
        size_data[imem++] = memory_padded_size(
            sizeof(double) * (N_SCHEMES + 1) * settings.n_materials *
            settings.n_energies, pad_size);
        /* elements_in. */
        size_data[imem++] =
            memory_padded_size(sizeof(int) * settings.n_materials, pad_size);
        /* material_density */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials, pad_size);
        /* material_ZoA */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials, pad_size);
        /* material_I */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials, pad_size);
        /* material_aS */
        size_data[imem++] =
            memory_padded_size(sizeof(double) * settings.n_materials, pad_size);
        /* element. */
        size_data[imem++] = memory_padded_size(sizeof(struct atomic_element *) *
                settings.n_elements, pad_size) +
            memory_padded_size(sizeof(struct atomic_element), pad_size) *
                settings.n_elements +
            settings.size_elements_names;
        /* Atomic composition. */
        size_data[imem++] =
            memory_padded_size(
                sizeof(struct material_component *) * settings.n_materials,
                pad_size) +
            memory_padded_size(
                settings.n_components * sizeof(struct material_component),
                pad_size);
        /* Composite material. */
        size_data[imem++] =
            memory_padded_size(
                sizeof(struct composite_material *) * settings.n_composites,
                pad_size) +
            settings.size_composite;
        /* material_name. */
        size_data[imem++] =
            memory_padded_size(sizeof(char *) * settings.n_materials +
                    settings.size_materials_names,
                pad_size);
        /* Bremsstrahlung model name. */
        size_data[imem++] = memory_padded_size(
            sizeof(char) * (strlen(opts.bremsstrahlung) + 1), pad_size);
        /* Pair production model name. */
        size_data[imem++] = memory_padded_size(
            sizeof(char) * (strlen(opts.pair_production) + 1), pad_size);
        /* Photonuclear model name. */
        size_data[imem++] = memory_padded_size(
            sizeof(char) * (strlen(opts.photonuclear) + 1), pad_size);

        /* Allocate the shared memory. */
        int size_total = 0;
        for (imem = 0; imem < N_DATA_POINTERS; imem++)
                size_total += size_data[imem];
        const int size_shared = sizeof(**physics_ptr) + size_total;
        void * tmp_ptr = reallocate(mdf, size_shared);
        if (tmp_ptr == NULL) {
                ERROR_REGISTER_MEMORY();
                goto clean_and_exit;
        }
        physics = tmp_ptr;
        *physics_ptr = physics;
        memset(physics, 0x0, size_shared);
        physics->size = size_shared;
        mdf = NULL;

        /* Map the data pointers. */
        double * p = physics->data;
        void ** ptr = (void **)(&(physics->mdf_path));
        for (imem = 0; imem < N_DATA_POINTERS; imem++) {
                *ptr = p;
                ptr++;
                p += size_data[imem] / pad_size;
        }

        /* Set the DCS's. */
        pumas_dcs_get(PUMAS_PROCESS_BREMSSTRAHLUNG, opts.bremsstrahlung,
            &physics->dcs_bremsstrahlung);
        strcpy(physics->model_bremsstrahlung, opts.bremsstrahlung);
        pumas_dcs_get(PUMAS_PROCESS_PAIR_PRODUCTION, opts.pair_production,
            &physics->dcs_pair_production);
        strcpy(physics->model_pair_production, opts.pair_production);
        pumas_dcs_get(PUMAS_PROCESS_PHOTONUCLEAR, opts.photonuclear,
            &physics->dcs_photonuclear);
        strcpy(physics->model_photonuclear, opts.photonuclear);

        /* Copy the global settings. */
        physics->particle = particle;
        if (particle == PUMAS_PARTICLE_MUON) {
                physics->ctau = MUON_C_TAU;
                physics->mass = MUON_MASS;
        } else {
                physics->ctau = TAU_C_TAU;
                physics->mass = TAU_MASS;
        }
        physics->n_energies = settings.n_energies;
        physics->n_materials = settings.n_materials;
        physics->n_composites = settings.n_composites;
        physics->n_elements = settings.n_elements;
        physics->n_components = settings.n_components;
        physics->n_table_dcs = n_table_dcs;
        physics->max_components = settings.max_components;
        physics->n_energy_loss_header = settings.n_energy_loss_header;
        strcpy(physics->mdf_path, file_mdf);

        /* Set the cutoff and elastic ratio */
        physics->cutoff = opts.cutoff;
        physics->elastic_ratio = opts.elastic_ratio;

        /* Allocate a new MDF buffer. */
        if ((mdf = allocate(sizeof(struct mdf_buffer) + size_mdf)) == NULL) {
                ERROR_REGISTER_MEMORY();
                goto clean_and_exit;
        }
        memcpy(mdf, &settings, sizeof(settings));

        /* Set the path to the dE/dX files. */
        strcpy(physics->dedx_path, dedx_path);

        /* Initialise dE/dX filenames */
        int imat;
        for (imat = 0; imat < physics->n_materials - physics->n_composites;
             imat++) {
                physics->dedx_filename[imat] = NULL;
        }

        /* Parse the elements. */
        if ((mdf_parse_elements(physics, mdf, error_)) != PUMAS_RETURN_SUCCESS)
                goto clean_and_exit;

        /* Parse the base materials. */
        if ((mdf_parse_materials(physics, mdf, error_)) != PUMAS_RETURN_SUCCESS)
                goto clean_and_exit;

        /* Parse the composite materials. */
        if ((mdf_parse_composites(physics, mdf, error_)) !=
            PUMAS_RETURN_SUCCESS)
                goto clean_and_exit;

        /* Compute the MEE from material's composition if not set by MDF. */
        for (imat = 0; imat < physics->n_materials - physics->n_composites;
             imat++) {
                if (physics->material_I[imat] <= 0.)
                        compute_MEE(physics, imat);
        }

        /* All done if in dry mode. */
        if (dry_mode) goto clean_and_exit;

        /* Precompute the CEL integrals and the TT parameters. */
        for (imat = 0; imat < physics->n_materials - physics->n_composites;
             imat++) {
                /* Set the scalling factor */
                struct atomic_shell * shells = atomic_shell_create(
                    physics, imat, NULL, physics->material_aS + imat);
                if (shells == NULL) {
                        ERROR_REGISTER_MEMORY();
                        goto clean_and_exit;
                }
                deallocate(shells);

                compute_cel_integrals(physics, imat);
                compute_csda_magnetic_transport(physics, imat);
                if (compute_scattering(physics, imat, error_) !=
                    PUMAS_RETURN_SUCCESS) goto clean_and_exit;
        }

        /* Precompute the same properties for composite materials. */
        for (imat = physics->n_materials - physics->n_composites;
             imat < physics->n_materials; imat++) {
                if (((compute_composite(physics, imat, error_)) !=
                        PUMAS_RETURN_SUCCESS) ||
                    ((compute_composite_density(physics, imat, error_)) !=
                        PUMAS_RETURN_SUCCESS))
                        goto clean_and_exit;
        }

        /* Tabulate the DCS for atomic elements. */
        int iel;
        for (iel = 0; iel < physics->n_elements; iel++) {
                if (compute_dcs_table(physics, iel, error_) !=
                    PUMAS_RETURN_SUCCESS) goto clean_and_exit;
        }

        /* Compute the cubic interp. coefficients for atomic elements
         * cross-sections
         */
        compute_pchip_elements_coeffs(physics);

clean_and_exit:
        if (fid_mdf != NULL) fclose(fid_mdf);
        deallocate(mdf);
        io_read_line(NULL, NULL, NULL, 0, error_);
        compute_scattering_parameters(physics, -1, -1, error_);
        compute_msc_soft(physics, -1, NULL, error_);
        compute_cel_and_del(physics, -1);
        compute_dcs_table(physics, -1, error_);
        if ((error_->code != PUMAS_RETURN_SUCCESS) && (physics != NULL)) {
                deallocate(physics);
                *physics_ptr = NULL;
        }

        return ERROR_RAISE();
}

/* The standard API initialisation. */
enum pumas_return pumas_physics_create(struct pumas_physics ** physics,
    enum pumas_particle particle, const char * mdf_path, const char * dedx_path,
    const struct pumas_physics_settings * settings)
{
        ERROR_INITIALISE(pumas_physics_create);

        /* Load the MDF in dry mode first */
        enum pumas_return rc;
        if ((rc = _initialise(physics, particle, mdf_path, dedx_path, 1,
            settings)) != PUMAS_RETURN_SUCCESS) {
                return rc;
        }

        /* Check for missing energy loss files */
        struct pumas_physics * p = *physics;
        struct physics_tabulation_data data = {
                .n_energies = (settings == NULL) ? 0 : settings->n_energies,
                .energy = ((settings == NULL) || (settings->n_energies <= 0)) ?
                    NULL : settings->energy,
                .overwrite = (settings == NULL) ? 0 : settings->update,
                .outdir = p->dedx_path,
                .work = NULL
        };
        double * energy = NULL;

        /* Prepare the filename buffer */
        char * filename = NULL;
        int offset_dir, size_name;
        if ((mdf_format_path(p->dedx_path, p->mdf_path, &filename, &offset_dir,
            &size_name, error_)) != PUMAS_RETURN_SUCCESS) {
                goto error;
        }

        const int nmat = p->n_materials - p->n_composites;
        int imat;
        for (imat = 0; imat < nmat; imat++) {
                const int size_new =
                    offset_dir + strlen(p->dedx_filename[imat]) + 1;
                if (size_new > size_name)
                        size_name = size_new;
        }
        {
                /* Get enough memory. */
                char * new_name = reallocate(filename, size_name);
                if (new_name == NULL) {
                        ERROR_REGISTER_MEMORY();
                        goto error;
                }
                filename = new_name;
        }

        if (!data.overwrite) {
                struct mdf_buffer mdf = {
                    .mdf_path = p->mdf_path,
                    .dry_mode = 1
                };

                for (imat = 0; imat < nmat; imat++) {
                        strcpy(filename + offset_dir, p->dedx_filename[imat]);

                        mdf_parse_kinetic(
                            &mdf, filename, data.n_energies, energy, error_);
                        if (error_->code == PUMAS_RETURN_PATH_ERROR) {
                                continue;
                        } else if (error_->code != PUMAS_RETURN_SUCCESS) {
                                goto error;
                        }
                        mdf.n_energies--;

                        if ((energy == NULL) && (mdf.n_energies > 0)) {
                                energy = allocate(
                                    mdf.n_energies * sizeof *energy);
                                if (energy == NULL) {
                                        ERROR_REGISTER_MEMORY();
                                        goto error;
                                }
                                data.n_energies = mdf.n_energies;
                                data.energy = energy;
                                mdf_parse_kinetic(
                                    &mdf, filename, -1, energy, error_);
                        } else {
                                if (mdf.n_energies != data.n_energies) {
                                        ERROR_VREGISTER(
                                            PUMAS_RETURN_FORMAT_ERROR,
                                            "inconsistent number of rows (%d) "
                                            "for file `%s'",
                                            mdf.n_energies, filename);
                                        goto error;
                                }
                        }
                }
        }

        /* Force generate (missing) tables */
        for (imat = 0; imat < nmat; imat++) {
                strcpy(filename + offset_dir, p->dedx_filename[imat]);
                data.material = imat;

                if (data.overwrite) {
                        if (physics_tabulate(p, &data, error_) !=
                            PUMAS_RETURN_SUCCESS) goto error;
                } else {
                        FILE * stream = fopen(filename, "r");
                        if (stream == NULL) {
                                if (physics_tabulate(p, &data, error_) !=
                                    PUMAS_RETURN_SUCCESS) goto error;
                        } else {
                                fclose(stream);
                        }
                }
        }

        physics_tabulation_clear(p, &data);
        deallocate(energy);
        deallocate(filename);
        pumas_physics_destroy(physics);

        if (settings && settings->dry) {
                return PUMAS_RETURN_SUCCESS;
        } else {
                /* Load the full physics */
                return _initialise(
                    physics, particle, mdf_path, dedx_path, 0, settings);
        }

error:
        physics_tabulation_clear(p, &data);
        deallocate(energy);
        deallocate(filename);
        pumas_physics_destroy(physics);
        return ERROR_RAISE();
}

enum pumas_return pumas_physics_load(
    struct pumas_physics ** physics_ptr, FILE * stream)
{
        ERROR_INITIALISE(pumas_physics_load);

        /* Check the physics pointer. */
        if (physics_ptr == NULL) {
                return ERROR_NULL_PHYSICS();
        }

        /* Check the input stream */
        if (stream == NULL)
                return ERROR_MESSAGE(
                    PUMAS_RETURN_PATH_ERROR, "invalid input stream (null)");
#if (GDB_MODE)
        /* Save the floating points exceptions status and enable them. */
        if (!fe_initialised) {
                fe_status = fegetexcept();
                feclearexcept(FE_ALL_EXCEPT);
                feenableexcept(
                    FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW);
                fe_initialised = 1;
        }
#endif
        /* Check the binary dump tag. */
        struct pumas_physics * physics = NULL;
        int tag;
        if (fread(&tag, sizeof(tag), 1, stream) != 1) goto error;
        if (tag != PHYSICS_BINARY_DUMP_TAG) {
                ERROR_REGISTER(PUMAS_RETURN_FORMAT_ERROR,
                    "incompatible version of binary dump");
                goto error;
        }

        /* Allocate the container. */
        int size;
        if (fread(&size, sizeof(size), 1, stream) != 1) goto error;
        physics = allocate(size);
        *physics_ptr = physics;
        if (physics == NULL) {
                ERROR_REGISTER_MEMORY();
                goto error;
        }

        /* Load the data and remap the addresses. */
        if (fread(physics, size, 1, stream) != 1) goto error;

        void ** ptr = (void **)(&(physics->mdf_path));
        ptrdiff_t delta = (char *)(physics->data) - (char *)(*ptr);
        int i;
        for (i = 0; i < N_DATA_POINTERS; i++, ptr++)
                *ptr = ((char *)(*ptr)) + delta;

        struct atomic_element ** element = physics->element;
        for (i = 0; i < physics->n_elements; i++) {
                element[i] =
                    (struct atomic_element *)(((char *)element[i]) + delta);
                element[i]->name += delta;
        }

        struct material_component ** composition = physics->composition;
        for (i = 0; i < physics->n_materials; i++)
                composition[i] =
                    (struct material_component *)(((char *)composition[i]) +
                        delta);

        struct composite_material ** composite = physics->composite;
        for (i = 0; i < physics->n_composites; i++)
                composite[i] =
                    (struct composite_material *)(((char *)composite[i]) +
                        delta);

        char ** material_name = physics->material_name;
        for (i = 0; i < physics->n_materials; i++) material_name[i] += delta;

        /* Set the DCS models */
        if (dcs_check_model(PUMAS_PROCESS_BREMSSTRAHLUNG,
            physics->model_bremsstrahlung, error_) == PUMAS_RETURN_SUCCESS) {
                pumas_dcs_get(PUMAS_PROCESS_BREMSSTRAHLUNG,
                    physics->model_bremsstrahlung,
                    &physics->dcs_bremsstrahlung);
        } else {
                goto error;
        }

        if (dcs_check_model(PUMAS_PROCESS_PAIR_PRODUCTION,
            physics->model_pair_production, error_) == PUMAS_RETURN_SUCCESS) {
                pumas_dcs_get(PUMAS_PROCESS_PAIR_PRODUCTION,
                    physics->model_pair_production,
                    &physics->dcs_pair_production);
        } else {
                goto error;
        }

        if (dcs_check_model(PUMAS_PROCESS_PHOTONUCLEAR,
            physics->model_photonuclear, error_) == PUMAS_RETURN_SUCCESS) {
                pumas_dcs_get(PUMAS_PROCESS_PHOTONUCLEAR,
                    physics->model_photonuclear,
                    &physics->dcs_photonuclear);
        } else {
                goto error;
        }

        /* Erase the dE/dX filename(s) */
        for (i = 0; i < physics->n_materials - physics->n_composites; i++) {
                physics->dedx_filename[i] = NULL;
        }

        return PUMAS_RETURN_SUCCESS;

error:
        deallocate(physics);
        *physics_ptr = NULL;
        return ERROR_RAISE();

#undef N_DATA_POINTERS
}

enum pumas_return pumas_physics_dump(
    const struct pumas_physics * physics, FILE * stream)
{
        ERROR_INITIALISE(pumas_physics_dump);

        /* Check if the Physics is initialised. */
        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        /* Check the output stream */
        if (stream == NULL)
                return ERROR_MESSAGE(
                    PUMAS_RETURN_PATH_ERROR, "invalid output stream (null)");

        /* Dump the configuration. */
        int tag = PHYSICS_BINARY_DUMP_TAG;
        if (fwrite(&tag, sizeof(tag), 1, stream) != 1) goto error;
        if (fwrite(&physics->size, sizeof(physics->size), 1, stream) != 1)
                goto error;
        if (fwrite(physics, physics->size, 1, stream) != 1) goto error;

        return PUMAS_RETURN_SUCCESS;

error:
        return ERROR_MESSAGE(
            PUMAS_RETURN_IO_ERROR, "could not write to dump file");

#undef PHYSICS_BINARY_DUMP_TAG
}

void pumas_physics_destroy(struct pumas_physics ** physics_ptr)
{
        if ((physics_ptr == NULL) || (*physics_ptr == NULL)) return;

        /* Free the shared data. */
        struct pumas_physics * physics = *physics_ptr;
        int i;
        for (i = 0; i < physics->n_materials - physics->n_composites; i++) {
                deallocate(physics->dedx_filename[i]);
                physics->dedx_filename[i] = NULL;
        }
        deallocate(physics);
        *physics_ptr = NULL;
}

double pumas_physics_cutoff(const struct pumas_physics * physics)
{
        return (physics == NULL) ? -1 : physics->cutoff;
}

double pumas_physics_elastic_ratio(const struct pumas_physics * physics)
{
        return (physics == NULL) ? -1 : physics->elastic_ratio;
}

const char * pumas_error_function(pumas_function_t * caller)
{
#define TOSTRING(function)                                                     \
        if (caller == (pumas_function_t *)function) return #function;

        /* Library functions with an error code. */
        TOSTRING(pumas_physics_create)
        TOSTRING(pumas_physics_dump)
        TOSTRING(pumas_physics_load)
        TOSTRING(pumas_context_transport)
        TOSTRING(pumas_physics_particle)
        TOSTRING(pumas_context_create)
        TOSTRING(pumas_context_random_dump)
        TOSTRING(pumas_context_random_load)
        TOSTRING(pumas_context_random_seed_get)
        TOSTRING(pumas_context_random_seed_set)
        TOSTRING(pumas_recorder_create)
        TOSTRING(pumas_physics_dcs)
        TOSTRING(pumas_physics_element_name)
        TOSTRING(pumas_physics_element_index)
        TOSTRING(pumas_physics_element_properties)
        TOSTRING(pumas_physics_material_name)
        TOSTRING(pumas_physics_material_index)
        TOSTRING(pumas_physics_material_properties)
        TOSTRING(pumas_physics_composite_update)
        TOSTRING(pumas_physics_composite_properties)
        TOSTRING(pumas_physics_print)
        TOSTRING(pumas_error_raise)
        TOSTRING(pumas_physics_property_range)
        TOSTRING(pumas_physics_property_proper_time)
        TOSTRING(pumas_physics_property_magnetic_rotation)
        TOSTRING(pumas_physics_property_kinetic_energy)
        TOSTRING(pumas_physics_property_stopping_power)
        TOSTRING(pumas_physics_property_energy_straggling)
        TOSTRING(pumas_physics_property_elastic_path)
        TOSTRING(pumas_physics_property_elastic_cutoff_angle)
        TOSTRING(pumas_physics_property_transport_path)
        TOSTRING(pumas_physics_property_cross_section)
        TOSTRING(pumas_physics_table_value)
        TOSTRING(pumas_physics_table_index)
        TOSTRING(pumas_dcs_get)
        TOSTRING(pumas_dcs_range)
        TOSTRING(pumas_dcs_register)

        /* Other library functions. */
        TOSTRING(pumas_constant)
        TOSTRING(pumas_dcs_default)
        TOSTRING(pumas_elastic_dcs)
        TOSTRING(pumas_elastic_path)
        TOSTRING(pumas_electronic_dcs)
        TOSTRING(pumas_electronic_density_effect)
        TOSTRING(pumas_electronic_stopping_power)
        TOSTRING(pumas_physics_cutoff)
        TOSTRING(pumas_physics_elastic_ratio)
        TOSTRING(pumas_physics_destroy)
        TOSTRING(pumas_context_destroy)
        TOSTRING(pumas_context_physics_get)
        TOSTRING(pumas_recorder_clear)
        TOSTRING(pumas_recorder_destroy)
        TOSTRING(pumas_version)
        TOSTRING(pumas_error_function)
        TOSTRING(pumas_error_handler_set)
        TOSTRING(pumas_error_handler_get)
        TOSTRING(pumas_error_catch)
        TOSTRING(pumas_physics_element_length)
        TOSTRING(pumas_physics_material_length)
        TOSTRING(pumas_physics_composite_length)
        TOSTRING(pumas_physics_table_length)
        TOSTRING(pumas_memory_allocator)
        TOSTRING(pumas_memory_reallocator)
        TOSTRING(pumas_memory_deallocator)

        return NULL;
#undef TOSTRING
}

void pumas_error_handler_set(pumas_handler_cb * handler)
{
        s_error.handler = handler;
}

pumas_handler_cb * pumas_error_handler_get(void) { return s_error.handler; }

void pumas_error_catch(int catch)
{
        if (catch) {
                s_error.catch = 1;
                s_error.catch_error.code = PUMAS_RETURN_SUCCESS;
                s_error.catch_error.function = NULL;
        } else {
                s_error.catch = 0;
        }
}

enum pumas_return pumas_error_raise(void)
{
        ERROR_INITIALISE(pumas_error_raise);

        if (s_error.catch == 0)
                ERROR_MESSAGE(
                    PUMAS_RETURN_RAISE_ERROR, "`raise' called without `catch'");
        s_error.catch = 0;
        memcpy(error_, &s_error.catch_error, sizeof(*error_));
        return ERROR_RAISE();
}

/* Set the MT initial state */
static enum pumas_return random_initialise(struct pumas_context * context,
    const unsigned long * seed_ptr, struct error_context * error_)
{
        unsigned long seed;
        if (seed_ptr == NULL) {
                /* Sample the seed from the OS */
#ifdef _WIN32
                unsigned int tmp;
                if (rand_s(&tmp) != 0) {
                        return ERROR_REGISTER(PUMAS_RETURN_PATH_ERROR,
                                "could not read from `rand_s'");
                } else {
                        seed = tmp;
                }
#else
                size_t count = 0;
                FILE * fid = fopen("/dev/urandom", "r");
                if (fid != NULL) {
                        count = fread(&seed, sizeof seed, 1, fid);
                        fclose(fid);
                }

                if (count == 0) {
                        return ERROR_REGISTER(PUMAS_RETURN_PATH_ERROR,
                                "could not read from `/dev/urandom'");
                }
#endif
        } else {
                seed = *seed_ptr;
        }

        struct simulation_context * context_ = (void *)context;
        if (context_->random_data == NULL) {
                context_->random_data =
                    allocate(sizeof(*context_->random_data));
                if (context_->random_data == NULL) {
                        return ERROR_REGISTER_MEMORY();
                }
        }
        struct pumas_random_data * data = context_->random_data;

        memset(data, 0x0, sizeof (*data));
        data->buffer[0] = seed & 0xffffffffUL;
        int j;
        for (j = 1; j < MT_PERIOD; j++) {
                data->buffer[j] = (1812433253UL *
                        (data->buffer[j - 1] ^
                        (data->buffer[j - 1] >> 30)) +
                    j);
                data->buffer[j] &= 0xffffffffUL;
        }
        data->seed = seed;
        data->index = MT_PERIOD;

        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_context_random_seed_set(
    struct pumas_context * context, const unsigned long * seed_ptr)
{
        ERROR_INITIALISE(pumas_context_random_seed_set);

        random_initialise(context, seed_ptr, error_);

        return ERROR_RAISE();
}

enum pumas_return pumas_context_random_seed_get(
    struct pumas_context * context, unsigned long * seed_ptr)
{
        ERROR_INITIALISE(pumas_context_random_seed_get);

        struct simulation_context * context_ = (void *)context;
        if (context_->random_data == NULL) {
                enum pumas_return rc = random_initialise(context, NULL, error_);
                if (rc != PUMAS_RETURN_SUCCESS) {
                        *seed_ptr = 0;
                        return ERROR_RAISE();
                }
        }
        *seed_ptr = context_->random_data->seed;

        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_context_random_dump(
    struct pumas_context * context, FILE * stream)
{
        ERROR_INITIALISE(pumas_context_random_dump);

        /* Check the input stream */
        if (stream == NULL)
                return ERROR_MESSAGE(
                    PUMAS_RETURN_PATH_ERROR, "invalid input stream (null)");

        /* Write the version tag */
        const int tag = RANDOM_BINARY_DUMP_TAG;
        if (fwrite(&tag, sizeof(tag), 1, stream) != 1) goto error;

        /* Initialise the random engine if needed */
        struct simulation_context * context_ = (void *)context;
        if (context_->random_data == NULL) {
                enum pumas_return rc = random_initialise(context, NULL, error_);
                if (rc != PUMAS_RETURN_SUCCESS)
                        return ERROR_RAISE();
        }

        /* Dump the data */
        if (fwrite(context_->random_data, sizeof (*context_->random_data), 1,
            stream) != 1) goto error;

        return PUMAS_RETURN_SUCCESS;

error:
        return ERROR_MESSAGE(
            PUMAS_RETURN_IO_ERROR, "could not not write to stream");
}

enum pumas_return pumas_context_random_load(
    struct pumas_context * context, FILE * stream)
{
        ERROR_INITIALISE(pumas_context_random_load);

        /* Check the input stream */
        if (stream == NULL)
                return ERROR_MESSAGE(
                    PUMAS_RETURN_PATH_ERROR, "invalid input stream (null)");

        /* Check the binary dump tag */
        int tag;
        if (fread(&tag, sizeof(tag), 1, stream) != 1) goto error;
        if (tag != RANDOM_BINARY_DUMP_TAG) {
                ERROR_REGISTER(PUMAS_RETURN_FORMAT_ERROR,
                    "incompatible version of binary dump");
        }

        /* Allocate memory if needed */
        struct simulation_context * context_ = (void *)context;
        if (context_->random_data == NULL) {
                context_->random_data =
                    allocate(sizeof(*context_->random_data));
                if (context_->random_data == NULL) {
                        return ERROR_MESSAGE(PUMAS_RETURN_MEMORY_ERROR,
                            "could not allocate memory");
                }
        }

        /* Load the data */
        if (fread(context_->random_data, sizeof (*context_->random_data), 1,
            stream) != 1) goto error;

        return PUMAS_RETURN_SUCCESS;

error:
        return ERROR_MESSAGE(
            PUMAS_RETURN_IO_ERROR, "could not not read from stream");

#undef RANDOM_BINARY_DUMP_TAG
}

/* Uniform pseudo random distribution from a Mersenne Twister */
static double random_uniform01(struct pumas_context * context)
{
        /* Lazy initialisation of the MT if not already done */
        struct simulation_context * context_ = (void *)context;
        if (context_->random_data == NULL) {
                if (random_initialise(context, NULL, NULL) !=
                    PUMAS_RETURN_SUCCESS) return -1.;

        }
        struct pumas_random_data * data = context_->random_data;

        /* Check the buffer */
        if (data->index < MT_PERIOD - 1) {
                data->index++;
        } else {
                /* Update the MT state */
                const int M = 397;
                const unsigned long UPPER_MASK = 0x80000000UL;
                const unsigned long LOWER_MASK = 0x7fffffffUL;
                static unsigned long mag01[2] = { 0x0UL, 0x9908b0dfUL };
                unsigned long y;
                int kk;
                for (kk = 0; kk < MT_PERIOD - M; kk++) {
                        y = (data->buffer[kk] & UPPER_MASK) |
                            (data->buffer[kk + 1] & LOWER_MASK);
                        data->buffer[kk] = data->buffer[kk + M] ^
                            (y >> 1) ^ mag01[y & 0x1UL];
                }
                for (; kk < MT_PERIOD - 1; kk++) {
                        y = (data->buffer[kk] & UPPER_MASK) |
                            (data->buffer[kk + 1] & LOWER_MASK);
                        data->buffer[kk] =
                            data->buffer[kk + (M - MT_PERIOD)] ^
                            (y >> 1) ^ mag01[y & 0x1UL];
                }
                y = (data->buffer[MT_PERIOD - 1] & UPPER_MASK) |
                    (data->buffer[0] & LOWER_MASK);
                data->buffer[MT_PERIOD - 1] =
                    data->buffer[M - 1] ^ (y >> 1) ^ mag01[y & 0x1UL];
                data->index = 0;
        }

        /* Tempering */
        unsigned long y = data->buffer[data->index];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680UL;
        y ^= (y << 15) & 0xefc60000UL;
        y ^= (y >> 18);

        /* Convert to a floating point and return */
        return y * (1.0 / 4294967295.0);
}

/* Public library functions: simulation context management. */
enum pumas_return pumas_context_create(struct pumas_context ** context_,
    const struct pumas_physics * physics, int extra_memory)
{
        ERROR_INITIALISE(pumas_context_create);
        *context_ = NULL;

        /* Check the Physics initialisation. */
        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        /* Allocate the new context. */
        struct simulation_context * context;
        const int pad_size = sizeof(*(context->data));
        const int work_size =
            memory_padded_size(sizeof(struct coulomb_workspace) +
                    physics->max_components * sizeof(struct coulomb_data),
                pad_size);
        if (extra_memory < 0)
                extra_memory = 0;
        else
                extra_memory = memory_padded_size(extra_memory, pad_size);
        context = allocate(sizeof(*context) + work_size + extra_memory);
        if (context == NULL) {
                ERROR_REGISTER_MEMORY();
                return ERROR_RAISE();
        }

        /* Set the default configuration. */
        *context_ = (struct pumas_context *)context;
        context->physics = physics;
        context->extra_memory = extra_memory;
        if (extra_memory > 0)
                (*context_)->user_data = context->data + work_size / pad_size;
        else
                (*context_)->user_data = NULL;

        int imax = physics->n_energies - 2;
        context->index_K_last[0] = context->index_K_last[1] = imax;
        context->index_X_last[0] = context->index_X_last[1] = imax;

        context->random_data = NULL;
        (*context_)->random = &random_uniform01;

        (*context_)->medium = NULL;
        (*context_)->recorder = NULL;

        (*context_)->mode.decay = (physics->particle == PUMAS_PARTICLE_MUON) ?
            PUMAS_MODE_WEIGHTED :
            PUMAS_MODE_RANDOMISED;
        (*context_)->mode.direction = PUMAS_MODE_FORWARD;
        (*context_)->mode.energy_loss = PUMAS_MODE_STRAGGLED;
        (*context_)->mode.scattering = PUMAS_MODE_MIXED;
        (*context_)->event = PUMAS_EVENT_NONE;

        (*context_)->limit.energy = 0.;    /* GeV */
        (*context_)->limit.distance = 0.;  /* m */
        (*context_)->limit.grammage = 0.;  /* kg/m^2 */
        (*context_)->limit.time = 0.;      /* m/c */

        (*context_)->accuracy = DEFAULT_ACCURACY;

        /* Initialise the Gaussian transform of the random stream. */
        context->randn_done = 0;
        context->randn_next = 0.;

        /* Initialise the work space. */
        context->workspace = (struct coulomb_workspace *)context->data;

        return PUMAS_RETURN_SUCCESS;
}

void pumas_context_destroy(struct pumas_context ** context)
{
        /* Check that the context hasn't already been destroyed */
        if ((context == NULL) || (*context == NULL)) return;

        /* Release the memory */
        struct simulation_context * context_ = (void *)(*context);
        deallocate(context_->random_data);
        deallocate(*context);
        *context = NULL;
}

const struct pumas_physics * pumas_context_physics_get(
    const struct pumas_context * context)
{
        if (context == NULL) return NULL;

        const struct simulation_context * context_ =
            (const struct simulation_context *)context;
        return context_->physics;
}

/* Public library functions: global print routines */
enum pumas_return pumas_physics_print(const struct pumas_physics * physics,
    FILE * stream, const char * tabulation, const char * newline)
{
        ERROR_INITIALISE(pumas_physics_print);

        const char * tab = (tabulation == NULL) ? "" : tabulation;
        const char * cr = (newline == NULL) ? "" : newline;

        /* Check the Physics initialisation */
        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        /* Check the output stream */
        if (stream == NULL) goto error;

        /* Print the physics info */
        if (fprintf(stream, "{%s%s\"physics\" : {%s%s%s\"cutoff (%%)\""
                            " : %.5lg",
                cr, tab, cr, tab, tab, 100. * physics->cutoff) < 0)
                goto error;
        if (fprintf(stream, ",%s%s%s\"elastic_ratio (%%)\" : %.5lg", cr,
                tab, tab, 100. * physics->elastic_ratio) < 0)
                goto error;
        if (fprintf(stream, ",%s%s%s\"bremsstrahlung\" : \"%s\"", cr,
                tab, tab, physics->model_bremsstrahlung) < 0)
                goto error;
        if (fprintf(stream, ",%s%s%s\"pair_production\" : \"%s\"", cr,
                tab, tab, physics->model_pair_production) < 0)
                goto error;
        if (fprintf(stream, ",%s%s%s\"photonuclear\" : \"%s\"", cr,
                tab, tab, physics->model_photonuclear) < 0)
                goto error;
        if (fprintf(stream, ",%s%s%s\"n_energies\" : %d%s%s}", cr,
                tab, tab, physics->n_energies, cr, tab) < 0)
                goto error;

        /* Print the particle info */
        const char * ptype = (physics->particle == PUMAS_PARTICLE_MUON) ?
            "muon" : "tau";
        if (fprintf(stream, ",%s%s\"particle\" : {%s%s%s\"type\" : \"%s\"",
                cr, tab, cr, tab, tab, ptype) < 0)
                goto error;
        if (fprintf(stream, ",%s%s%s\"mass (GeV/c^2)\" : %.6lf", cr, tab,
                tab, physics->mass) < 0)
                goto error;
        if (fprintf(stream, ",%s%s%s\"lifetime (m/c)\" : %.3lf%s%s}", cr, tab,
                tab, physics->ctau, cr, tab) < 0)
                goto error;

        /* Print the atomic elements */
        if (fprintf(stream, ",%s%s\"elements\" : {", cr, tab) < 0) goto error;
        int iel = 0;
        for (; iel < physics->n_elements; iel++) {
                const char * head = (iel == 0) ? "" : ",";
                const struct atomic_element * element = physics->element[iel];
                if (fprintf(stream, "%s%s%s%s\"%s\" : {", head, cr, tab, tab,
                        element->name) < 0)
                        goto error;
                if (fprintf(stream, "%s%s%s%s\"Z\" : %.0lf", cr, tab, tab, tab,
                        element->Z) < 0)
                        goto error;
                if (fprintf(stream, ",%s%s%s%s\"A (g/mol)\" : %.5lg", cr, tab,
                        tab, tab, element->A) < 0)
                        goto error;
                if (fprintf(stream, ",%s%s%s%s\"I (eV)\" : %.1lf%s%s%s}", cr,
                        tab, tab, tab, element->I * 1E+09, cr, tab, tab) < 0)
                        goto error;
        }
        if (fprintf(stream, "%s%s}", cr, tab) < 0) goto error;

        /* Print the materials */
        if (fprintf(stream, ",%s%s\"materials\" : {", cr, tab) < 0) goto error;
        int material = 0;
        for (; material < physics->n_materials - physics->n_composites;
             material++) {
                const char * head = (material == 0) ? "" : ",";
                if (fprintf(stream, "%s%s%s%s\"%s\" : {", head, cr, tab, tab,
                        physics->material_name[material]) < 0)
                        goto error;
                if (fprintf(stream, "%s%s%s%s\"density\" : %.5lg", cr, tab, tab,
                        tab, physics->material_density[material] * 1E-03) < 0)
                        goto error;
                if (fprintf(stream, ",%s%s%s%s\"elements\" : {", cr, tab, tab,
                        tab) < 0)
                        goto error;
                int iel = 0;
                for (; iel < physics->elements_in[material]; iel++) {
                        const char * head2 = (iel == 0) ? "" : ",";
                        int element =
                            physics->composition[material][iel].element;
                        if (fprintf(stream, "%s%s%s%s%s%s\"%s (%%)\" : %.5lg",
                                head2, cr, tab, tab, tab, tab,
                                physics->element[element]->name, 100. *
                                    physics->composition[material][iel]
                                        .fraction) < 0)
                                goto error;
                }
                if (fprintf(stream, "%s%s%s%s}%s%s%s}",
                    cr, tab, tab, tab, cr, tab, tab) < 0)
                        goto error;
        }
        if (fprintf(stream, "%s%s}", cr, tab) < 0) goto error;
        if (physics->n_composites <= 0) goto closure;

        /* Print the composites */
        if (fprintf(stream, ",%s%s\"composites\" : {", cr, tab) < 0) goto error;
        const int material0 = physics->n_materials - physics->n_composites;
        material = material0;
        for (; material < physics->n_materials; material++) {
                const char * head = (material == material0) ? "" : ",";
                struct composite_material * composite =
                    physics->composite[material - material0];
                if (fprintf(stream, "%s%s%s%s\"%s\" : {", head, cr, tab, tab,
                        physics->material_name[material]) < 0)
                        goto error;
                if (fprintf(stream, "%s%s%s%s\"density\" : %.5lg", cr, tab, tab,
                        tab, physics->material_density[material] * 1E-03) < 0)
                        goto error;
                if (fprintf(stream, ",%s%s%s%s\"materials\" : {", cr, tab, tab,
                        tab) < 0)
                        goto error;

                int imat = 0;
                for (; imat < composite->n_components; imat++) {
                        const char * head2 = (imat == 0) ? "" : ",";
                        struct composite_component * c =
                            composite->component + imat;
                        if (fprintf(stream, "%s%s%s%s%s%s\"%s (%%)\" : %.5lg",
                                head2, cr, tab, tab, tab, tab,
                                physics->material_name[c->material],
                                100. * c->fraction) < 0)
                                goto error;
                }
                if (fprintf(stream, "%s%s%s%s}%s%s%s}", cr, tab, tab, tab, cr,
                        tab, tab) < 0)
                        goto error;
        }
        if (fprintf(stream, "%s%s}", cr, tab) < 0) goto error;

closure:
        if (fprintf(stream, "%s}", cr) < 0) goto error;

        return PUMAS_RETURN_SUCCESS;
error:
        return ERROR_MESSAGE(
            PUMAS_RETURN_IO_ERROR, "could not write to stream");
}

void pumas_version(int * major, int * minor, int * patch)
{
        if (major != NULL) *major = PUMAS_VERSION_MAJOR;
        if (minor != NULL) *minor = PUMAS_VERSION_MINOR;
        if (patch != NULL) *patch = PUMAS_VERSION_PATCH;
}

/* Public library functions: recorder handling. */
enum pumas_return pumas_recorder_create(
    struct pumas_recorder ** recorder_, int extra_memory)
{
        ERROR_INITIALISE(pumas_recorder_create);
        *recorder_ = NULL;

        /*  Allocate memory for the new recorder. */
        struct frame_recorder * recorder = NULL;
        if (extra_memory < 0) extra_memory = 0;
        recorder = allocate(sizeof(*recorder) + extra_memory);
        if (recorder == NULL) {
                ERROR_REGISTER_MEMORY();
                return ERROR_RAISE();
        }

        /* Configure the context in order to use the new recorder. */
        *recorder_ = (struct pumas_recorder *)recorder;

        /* configure the new recorder and return it. */
        (*recorder_)->period = 1;
        (*recorder_)->record = NULL;
        (*recorder_)->length = 0;
        (*recorder_)->first = NULL;
        (*recorder_)->user_data = (extra_memory > 0) ? recorder->data : NULL;
        recorder->last = NULL;
        recorder->stack = NULL;

        return PUMAS_RETURN_SUCCESS;
}

void pumas_recorder_destroy(struct pumas_recorder ** recorder)
{
        if ((recorder == NULL) || (*recorder == NULL)) return;
        pumas_recorder_clear(*recorder);
        deallocate(*recorder);
        *recorder = NULL;
}

void pumas_recorder_clear(struct pumas_recorder * recorder)
{
        if (recorder == NULL) return;

        struct frame_recorder * const rec =
            (struct frame_recorder * const)recorder;
        struct frame_stack * current = rec->stack;
        while (current != NULL) {
                struct frame_stack * next = current->next;
                deallocate(current);
                current = next;
        }
        rec->stack = NULL;
        rec->last = NULL;
        recorder->first = NULL;
        recorder->length = 0;
}

/* Public library functions: properties accessors. */
enum pumas_return pumas_physics_property_range(
    const struct pumas_physics * physics, enum pumas_mode scheme,
    int material, double kinetic, double * grammage)
{
        ERROR_INITIALISE(pumas_physics_property_range);
        *grammage = 0.;

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        } else if ((scheme <= PUMAS_MODE_DISABLED) ||
            (scheme >= PUMAS_MODE_STRAGGLED)) {
                return ERROR_INVALID_SCHEME(scheme);
        } else if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        *grammage = cel_grammage(physics, NULL, scheme, material, kinetic);
        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_physics_property_proper_time(
    const struct pumas_physics * physics, enum pumas_mode scheme,
    int material, double kinetic, double * time)
{
        ERROR_INITIALISE(pumas_physics_property_proper_time);
        *time = 0.;

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        } else if ((scheme <= PUMAS_MODE_DISABLED) ||
            (scheme >= PUMAS_MODE_STRAGGLED)) {
                return ERROR_INVALID_SCHEME(scheme);
        } else if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        *time = cel_proper_time(physics, NULL, scheme, material, kinetic);
        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_physics_property_magnetic_rotation(
    const struct pumas_physics * physics, int material, double kinetic,
    double * angle)
{
        ERROR_INITIALISE(pumas_physics_property_magnetic_rotation);
        *angle = 0.;

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        } else if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        *angle = cel_magnetic_rotation(physics, NULL, material, kinetic);
        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_physics_property_kinetic_energy(
    const struct pumas_physics * physics, enum pumas_mode scheme,
    int material, double grammage, double * kinetic)
{
        ERROR_INITIALISE(pumas_physics_property_kinetic_energy);
        *kinetic = 0.;

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        } else if ((scheme <= PUMAS_MODE_DISABLED) ||
            (scheme >= PUMAS_MODE_STRAGGLED)) {
                return ERROR_INVALID_SCHEME(scheme);
        } else if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        *kinetic =
            cel_kinetic_energy(physics, NULL, scheme, material, grammage);
        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_physics_property_stopping_power(
    const struct pumas_physics * physics, enum pumas_mode scheme,
    int material, double kinetic, double * dedx)
{
        ERROR_INITIALISE(pumas_physics_property_stopping_power);
        *dedx = 0.;

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        } else if ((scheme <= PUMAS_MODE_DISABLED) ||
            (scheme >= PUMAS_MODE_STRAGGLED)) {
                return ERROR_INVALID_SCHEME(scheme);
        } else if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        *dedx = cel_energy_loss(physics, NULL, scheme, material, kinetic);
        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_physics_property_energy_straggling(
    const struct pumas_physics * physics, int material, double kinetic,
    double * straggling)
{
        ERROR_INITIALISE(pumas_physics_property_energy_straggling);
        *straggling = 0.;

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        } else if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        *straggling = cel_straggling(physics, NULL, material, kinetic);
        return PUMAS_RETURN_SUCCESS;
}

/* Public library function: elastic cutoff angle. */
enum pumas_return pumas_physics_property_elastic_cutoff_angle(
    const struct pumas_physics * physics, int material, double kinetic,
    double * angle)
{
        ERROR_INITIALISE(pumas_physics_property_elastic_cutoff_angle);

        if (physics == NULL) {
                *angle = 0.;
                return ERROR_NOT_INITIALISED();
        } else if ((material < 0) || (material >= physics->n_materials)) {
                *angle = 0.;
                return ERROR_INVALID_MATERIAL(material);
        }

        double mu0;
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 1)) {
                mu0 = *table_get_Mu0(physics, material, 1);
        } else if (kinetic >= *table_get_K(physics, imax)) {
                mu0 = *table_get_Mu0(physics, material, imax);
        } else {
                const double * x = table_get_K(physics, 0);
                const int i1 = table_index(physics, NULL, x, kinetic);
                x += i1;
                const double * y = table_get_Mu0(physics, material, i1);
                const double * m = table_get_Mu0_dK(physics, material, i1);
                const double dx = x[1] - x[0];
                const double t = (kinetic - x[0]) / dx;
                mu0 = math_pchip_interpolate(
                    t, y[0], y[1], m[0] * dx, m[1] * dx);
        }

        if (mu0 < 1E-08) {
                *angle = 2 * sqrt(mu0);
        } else {
                *angle = acos(1. - 2 * mu0);
        }

        return PUMAS_RETURN_SUCCESS;
}

/* Public library function: elastic path. */
enum pumas_return pumas_physics_property_elastic_path(
    const struct pumas_physics * physics, int material, double kinetic,
    double * length)
{
        ERROR_INITIALISE(pumas_physics_property_elastic_path);

        if (physics == NULL) {
                *length = 0.;
                return ERROR_NOT_INITIALISED();
        } else if ((material < 0) || (material >= physics->n_materials)) {
                *length = 0.;
                return ERROR_INVALID_MATERIAL(material);
        }

        const int imax = physics->n_energies - 1;
        double lp2;
        if (kinetic < *table_get_K(physics, 1)) {
                lp2 = *table_get_Lb(physics, material, 1);
        } else if (kinetic >= *table_get_K(physics, imax)) {
                lp2 = *table_get_Lb(physics, material, imax);
        } else {
                const int i1 = table_index(
                    physics, NULL, table_get_K(physics, 0), kinetic);
                const int i2 = i1 + 1;
                double h = (kinetic - *table_get_K(physics, i1)) /
                    (*table_get_K(physics, i2) - *table_get_K(physics, i1));
                lp2 = *table_get_Lb(physics, material, i1) +
                    h * (*table_get_Lb(physics, material, i2) -
                            *table_get_Lb(physics, material, i1));
        }
        const double p2 = kinetic * (kinetic + 2 * physics->mass);
        *length = lp2 / p2;

        return PUMAS_RETURN_SUCCESS;
}

/* Public library function: multiple scattering path length. */
enum pumas_return pumas_physics_property_transport_path(
    const struct pumas_physics * physics, enum pumas_mode scheme, int material,
    double kinetic, double * length)
{
        ERROR_INITIALISE(pumas_physics_property_transport_path);

        if (physics == NULL) {
                *length = 0.;
                return ERROR_NOT_INITIALISED();
        } else if ((scheme < PUMAS_MODE_DISABLED) ||
            (scheme >= PUMAS_MODE_STRAGGLED)) {
                return ERROR_INVALID_SCHEME(scheme);
        } else if ((material < 0) || (material >= physics->n_materials)) {
                *length = 0.;
                return ERROR_INVALID_MATERIAL(material);
        }

        double invlb1;
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 1)) {
                /* Use asymptotic limit as lb1 ~ sqrt(kinetic). */
                invlb1 = *table_get_Ms1(physics, scheme, material, 1) *
                    sqrt((*table_get_K(physics, 1)) / kinetic);
        } else if (kinetic >= *table_get_K(physics, imax)) {
                /* Use asymptotic limit as lb1 ~ kinetic. */
                invlb1 = *table_get_Ms1(physics, scheme, material, imax) *
                    (*table_get_K(physics, imax)) / kinetic;
        } else {
                const double * x = table_get_K(physics, 0);
                const int i1 = table_index(physics, NULL, x, kinetic);
                x += i1;
                const double * y = table_get_Ms1(physics, scheme, material, i1);
                const double * m = table_get_Ms1_dK(
                    physics, scheme, material, i1);
                const double dx = x[1] - x[0];
                const double t = (kinetic - x[0]) / dx;
                invlb1 = math_pchip_interpolate(
                    t, y[0], y[1], m[0] * dx, m[1] * dx);
        }
        *length = (invlb1 > 0.) ? 1. / invlb1 : DBL_MAX;

        return PUMAS_RETURN_SUCCESS;
}

/* Public library function: restricted inelastic and radiative cross section. */
enum pumas_return pumas_physics_property_cross_section(
    const struct pumas_physics * physics, int material, double kinetic,
    double * cross_section)
{
        ERROR_INITIALISE(pumas_physics_property_cross_section);
        *cross_section = 0.;

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        } else if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        *cross_section = (kinetic < *table_get_Kt(physics, material)) ?
            0. :
            del_cross_section(physics, NULL, material, kinetic);
        return PUMAS_RETURN_SUCCESS;
}

/* Public library function: the main transport routine. */
enum pumas_return pumas_context_transport(struct pumas_context * context,
    struct pumas_state * state, enum pumas_event * event,
    struct pumas_medium * media[2])
{
        ERROR_INITIALISE(pumas_context_transport);

        /* Check the context and state */
        if (context == NULL)
                return ERROR_MESSAGE(
                    PUMAS_RETURN_VALUE_ERROR, "no context (null)");
        if (state == NULL)
                return ERROR_MESSAGE(
                    PUMAS_RETURN_VALUE_ERROR, "no state (null)");

        /* Check the Physics initialisation */
        struct simulation_context * context_ =
            (struct simulation_context *)context;
        const struct pumas_physics * physics = context_->physics;
        if (physics == NULL) return ERROR_NOT_INITIALISED();

        /* Check the initial state. */
        if (state->decayed) {
                if (event != NULL) *event = PUMAS_EVENT_VERTEX_DECAY;
                return PUMAS_RETURN_SUCCESS;
        }

        /* Check the direction norm */
        {
                const double * const u = state->direction;
                const double norm2 = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
                if (fabs(norm2 - 1) > FLT_EPSILON) {
                        return ERROR_FORMAT(
                            PUMAS_RETURN_DIRECTION_ERROR,
                            "bad norm for state direction (norm^2 - 1 = %g)",
                            norm2 - 1);
                }
        }

        /* Check the configuration. */
        if (event != NULL) *event = PUMAS_EVENT_NONE;
        if (context->medium == NULL) {
                return ERROR_MESSAGE(
                    PUMAS_RETURN_MEDIUM_ERROR, "no medium specified");
        } else if ((physics->particle == PUMAS_PARTICLE_TAU) &&
            (context->mode.direction == PUMAS_MODE_FORWARD) &&
            (context->mode.decay == PUMAS_MODE_WEIGHTED)) {
                return ERROR_MESSAGE(PUMAS_RETURN_DECAY_ERROR,
                    "`PUMAS_MODE_WEIGHTED' mode is not valid for forward taus");
        }

        if ((context->accuracy <= 0) || (context->accuracy > 1)) {
                return ERROR_FORMAT(PUMAS_RETURN_ACCURACY_ERROR,
                    "bad accuracy value (expected a value in ]0,1], got %g)",
                    context->accuracy);
        }

        if ((context->mode.direction == PUMAS_MODE_BACKWARD) &&
            (context->mode.energy_loss > PUMAS_MODE_CSDA) &&
            (physics->cutoff < 1E-02)) {
                return ERROR_FORMAT(PUMAS_RETURN_ACCURACY_ERROR,
                    "bad cutoff value for backward transport (expected a "
                    "value greater than or equal to 0.01, got %g)",
                    physics->cutoff);
        }

        /* Get the start medium. */
        struct pumas_medium * medium;
        double step_max_medium;
        enum pumas_step step_max_type = context->medium(
            context, state, &medium, &step_max_medium);
        if (media != NULL) {
                media[0] = medium;
                media[1] = NULL;
        }
        if (medium == NULL) {
                if (event != NULL) *event = PUMAS_EVENT_MEDIUM;
                /* Register the start of the the track, if recording. */
                if (context->recorder != NULL)
                        record_state(context, medium, PUMAS_EVENT_MEDIUM |
                                PUMAS_EVENT_START | PUMAS_EVENT_STOP,
                            state);
                return PUMAS_RETURN_SUCCESS;
        } else if ((step_max_medium > 0.) &&
            (step_max_type == PUMAS_STEP_CHECK))
                step_max_medium += 0.5 * STEP_MIN;
        struct medium_locals locals = { { 0., { 0., 0., 0. }}, 0, physics };
        const double step_max_locals =
            transport_set_locals(context, medium, state, &locals);
        if ((step_max_locals > 0.) && (step_max_locals < step_max_medium))
                step_max_medium = step_max_locals;
        if (locals.api.density <= 0.) {
                ERROR_REGISTER_NEGATIVE_DENSITY(
                    physics->material_name[medium->material]);
                return ERROR_RAISE();
        }

        /* Randomise the lifetime, if required. */
        if (context->mode.decay == PUMAS_MODE_RANDOMISED) {
                if (context->random == NULL) {
                        return ERROR_MESSAGE(PUMAS_RETURN_MISSING_RANDOM,
                            "no random engine specified");
                }
                const double u = context->random(context);
                context_->lifetime = state->time - physics->ctau * log(u);
        }

        /* Call the relevant transport engine. */
        int do_stepping = 1;
        enum pumas_event e = PUMAS_EVENT_NONE;
        if ((step_max_medium <= 0.) && (step_max_locals <= 0.) &&
            (context->mode.energy_loss <= PUMAS_MODE_CSDA)) {
                /* This is an infinite and uniform medium. */
                if ((context->mode.energy_loss == PUMAS_MODE_DISABLED) &&
                    ((context->event & PUMAS_EVENT_LIMIT) == 0)) {
                        return ERROR_MESSAGE(PUMAS_RETURN_MISSING_LIMIT,
                            "infinite medium without external limit(s)");
                } else if (
                    (context->mode.scattering == PUMAS_MODE_DISABLED) &&
                    (context->mode.energy_loss == PUMAS_MODE_CSDA)) {
                        do_stepping = 0;
                }
        }

        if (do_stepping) {
                /* Transport with a detailed stepping. */
                e = transport_with_stepping(physics, context, state, &medium,
                    &locals, step_max_medium, step_max_type, step_max_locals,
                    error_);
        } else {
                /* This is a purely deterministic case. */
                e = transport_with_csda(
                    physics, context, state, medium, &locals, error_);
        }

        if (event != NULL) *event = e;
        if (media != NULL) media[1] = medium;
        return ERROR_RAISE();
}

/* Public library function: transported particle info. */
enum pumas_return pumas_physics_particle(const struct pumas_physics * physics,
    enum pumas_particle * particle, double * lifetime, double * mass)
{
        ERROR_INITIALISE(pumas_physics_particle);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }
        if (particle != NULL) *particle = physics->particle;
        if (lifetime != NULL) *lifetime = physics->ctau;
        if (mass != NULL) *mass = physics->mass;

        return PUMAS_RETURN_SUCCESS;
}

/* Public library functions: elements handling. */
enum pumas_return pumas_physics_element_name(
    const struct pumas_physics * physics, int index, const char ** element)
{
        ERROR_INITIALISE(pumas_physics_element_name);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }
        if ((index < 0) || (index >= physics->n_elements)) {
                return ERROR_INVALID_ELEMENT(index);
        }
        *element = physics->element[index]->name;

        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_physics_element_index(
    const struct pumas_physics * physics, const char * element, int * index)
{
        ERROR_INITIALISE(pumas_physics_element_index);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        const int i = element_index(physics, element);
        if (i < 0) {
                return ERROR_FORMAT(PUMAS_RETURN_UNKNOWN_ELEMENT,
                    "unknown element `%s'", element);
        } else {
                *index = i;
        }

        return PUMAS_RETURN_SUCCESS;
}

int pumas_physics_element_length(const struct pumas_physics * physics)
{
        if (physics == NULL) return 0;
        return physics->n_elements;
}

enum pumas_return pumas_physics_element_properties(
    const struct pumas_physics * physics, int index, double * Z, double * A,
    double * I)
{
        ERROR_INITIALISE(pumas_physics_element_properties);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }
        if ((index < 0) || (index >= physics->n_elements)) {
                return ERROR_INVALID_ELEMENT(index);
        }

        if (Z != NULL) *Z = physics->element[index]->Z;
        if (A != NULL) *A = physics->element[index]->A;
        if (I != NULL) *I = physics->element[index]->I;

        return PUMAS_RETURN_SUCCESS;
}

/* Public library functions: materials handling. */
enum pumas_return pumas_physics_material_name(
    const struct pumas_physics * physics, int index, const char ** material)
{
        ERROR_INITIALISE(pumas_physics_material_name);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }
        if ((index < 0) || (index >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(index);
        }
        *material = physics->material_name[index];

        return PUMAS_RETURN_SUCCESS;
}

enum pumas_return pumas_physics_material_index(
    const struct pumas_physics * physics, const char * material, int * index)
{
        ERROR_INITIALISE(pumas_physics_material_index);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }
        material_index(physics, material, index, error_);

        return ERROR_RAISE();
}

enum pumas_return pumas_physics_material_properties(
    const struct pumas_physics * physics, int material, int * length,
    double * density, double * I, int * components, double * fractions)
{
        ERROR_INITIALISE(pumas_physics_material_properties);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        const int i0 = physics->n_materials;
        if ((material < 0) || (material > i0 - 1)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        if (length != NULL) *length = physics->elements_in[material];
        if (density != NULL) *density = physics->material_density[material];
        if (I != NULL) *I = physics->material_I[material];
        int i;
        for (i = 0; i < physics->elements_in[material]; i++) {
                const struct material_component * component =
                    &physics->composition[material][i];
                if (components != NULL) components[i] = component->element;
                if (fractions != NULL) fractions[i] = component->fraction;
        }

        return PUMAS_RETURN_SUCCESS;
}

int pumas_physics_material_length(const struct pumas_physics * physics)
{
        if (physics == NULL) return 0;
        return physics->n_materials;
}

/* Public library functions: accessing and modifying composite materials. */
int pumas_physics_composite_length(const struct pumas_physics * physics)
{
        if (physics == NULL) return 0;
        return physics->n_composites;
}

enum pumas_return pumas_physics_composite_update(struct pumas_physics * physics,
    int material, const double * fractions)
{
        ERROR_INITIALISE(pumas_physics_composite_update);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        if (fractions == NULL) {
                return ERROR_MESSAGE(PUMAS_RETURN_VALUE_ERROR,
                    "NULL pointer for fractions");
        }

        const int i0 = physics->n_materials - physics->n_composites;
        if ((material < i0) || (material > physics->n_materials - 1)) {
                return ERROR_FORMAT(PUMAS_RETURN_INDEX_ERROR,
                    "invalid index for composite material [%d]", material);
        }

        const int icomp =
            material - physics->n_materials + physics->n_composites;
        int i;
        for (i = 0; i < physics->composite[icomp]->n_components; i++) {
                struct composite_component * component =
                    physics->composite[icomp]->component + i;
                const double d = fractions[i];
                component->fraction = (d > 0.) ? d : 0.;
        }

        const enum pumas_return rc =
            compute_composite_density(physics, material, error_);
        if ((rc != PUMAS_RETURN_SUCCESS) || (fractions == NULL))
                goto clean_and_exit;
        compute_composite(physics, material, error_);

clean_and_exit:
        /* Free temporary workspace and return. */
        compute_scattering_parameters(physics, -1, -1, error_);
        return ERROR_RAISE();
}

enum pumas_return pumas_physics_composite_properties(
    const struct pumas_physics * physics, int material, int * length,
    int * components, double * fractions)
{
        ERROR_INITIALISE(pumas_physics_composite_properties);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        const int i0 = physics->n_materials - physics->n_composites;
        if ((material < i0) || (material > physics->n_materials - 1)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        const int icomp =
            material - physics->n_materials + physics->n_composites;
        if (length != NULL) *length = physics->composite[icomp]->n_components;
        int i;
        for (i = 0; i < physics->composite[icomp]->n_components; i++) {
                struct composite_component * component =
                    physics->composite[icomp]->component + i;
                if (components != NULL) components[i] = component->material;
                if (fractions != NULL) fractions[i] = component->fraction;
        }

        return PUMAS_RETURN_SUCCESS;
}

/* Public library functions: info on tabulations. */
enum pumas_return pumas_physics_table_value(
    const struct pumas_physics * physics, enum pumas_property property,
    enum pumas_mode scheme, int material, int row_, double * value)
{
        ERROR_INITIALISE(pumas_physics_table_value);

        /* Check the input parameters. */
        *value = 0.;
        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        const int row = (row_ < 0) ? physics->n_energies + row_ : row_;

        if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        } else if ((row < 0) || (row >= physics->n_energies)) {
                return ERROR_FORMAT(
                    PUMAS_RETURN_INDEX_ERROR, "invalid `row' index [%d]", row_);
        }

        if (property == PUMAS_PROPERTY_KINETIC_ENERGY) {
                *value = *table_get_K(physics, row);
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_RANGE) {
                if ((scheme <= PUMAS_MODE_DISABLED) ||
                    (scheme >= PUMAS_MODE_STRAGGLED)) {
                        return ERROR_INVALID_SCHEME(scheme);
                }
                *value = *table_get_X(physics, scheme, material, row);
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_PROPER_TIME) {
                if ((scheme <= PUMAS_MODE_DISABLED) ||
                    (scheme >= PUMAS_MODE_STRAGGLED)) {
                        return ERROR_INVALID_SCHEME(scheme);
                }
                *value = *table_get_T(physics, scheme, material, row);
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_STOPPING_POWER) {
                if ((scheme <= PUMAS_MODE_DISABLED) ||
                    (scheme >= PUMAS_MODE_STRAGGLED)) {
                        return ERROR_INVALID_SCHEME(scheme);
                }
                *value = *table_get_dE(physics, scheme, material, row);
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_MAGNETIC_ROTATION) {
                const double factor = LARMOR_FACTOR / physics->mass;
                *value =
                    *table_get_T(physics, PUMAS_MODE_CSDA, material, row) *
                    factor;
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_CROSS_SECTION) {
                *value = *table_get_CS(physics, material, row);
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_ELASTIC_PATH) {
                if (row == 0) {
                        *value = 0.;
                } else {
                        const double k = *table_get_K(physics, row);
                        const double p2 = k * (k + 2 * physics->mass);
                        *value = *table_get_Lb(physics, material, row) / p2;
                }
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_ELASTIC_CUTOFF_ANGLE) {
                const double mu0 = *table_get_Mu0(physics, material, row);
                if (mu0 < 1E-08) {
                        *value = 2 * sqrt(mu0);
                } else {
                        *value = acos(1. - 2 * mu0);
                }
                return PUMAS_RETURN_SUCCESS;
        } else if (property == PUMAS_PROPERTY_TRANSPORT_PATH) {
                if ((scheme < PUMAS_MODE_DISABLED) ||
                    (scheme >= PUMAS_MODE_STRAGGLED)) {
                        return ERROR_INVALID_SCHEME(scheme);
                }
                const double invlb1 = *table_get_Ms1(
                    physics, scheme, material, row);
                *value = (invlb1 > 0.) ? 1. / invlb1 : 0.;
                return PUMAS_RETURN_SUCCESS;
        } else {
                return ERROR_FORMAT(PUMAS_RETURN_INDEX_ERROR,
                    "invalid `property' index [%d]", property);
        }
}

int pumas_physics_table_length(const struct pumas_physics * physics)
{
        if (physics == NULL) return 0;
        return physics->n_energies;
}

enum pumas_return pumas_physics_table_index(
    const struct pumas_physics * physics, enum pumas_property property,
    enum pumas_mode scheme, int material, double value, int * index)
{
        ERROR_INITIALISE(pumas_physics_table_index);

        /* Check some input parameters. */
        *index = 0;
        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }
        if ((material < 0) || (material >= physics->n_materials)) {
                return ERROR_INVALID_MATERIAL(material);
        }

        /* Get the tabulated value's index. */
        const double * table;
        if (property == PUMAS_PROPERTY_KINETIC_ENERGY)
                table = table_get_K(physics, 0);
        else if (property == PUMAS_PROPERTY_RANGE) {
                if ((scheme <= PUMAS_MODE_DISABLED) ||
                    (scheme >= PUMAS_MODE_STRAGGLED)) {
                        return ERROR_INVALID_SCHEME(scheme);
                }
                table = table_get_X(physics, scheme, material, 0);
        } else if (property == PUMAS_PROPERTY_PROPER_TIME) {
                if ((scheme <= PUMAS_MODE_DISABLED) ||
                    (scheme >= PUMAS_MODE_STRAGGLED)) {
                        return ERROR_INVALID_SCHEME(scheme);
                }
                table = table_get_T(physics, scheme, material, 0);
        } else if (property == PUMAS_PROPERTY_MAGNETIC_ROTATION) {
                value *= physics->mass / LARMOR_FACTOR;
                table = table_get_T(physics, PUMAS_MODE_CSDA, material, 0);
        } else {
                return ERROR_FORMAT(PUMAS_RETURN_INDEX_ERROR,
                    "invalid `property' index [%d]", property);
        }

        const int imax = physics->n_energies - 1;
        if (value < table[0]) {
                return ERROR_FORMAT(PUMAS_RETURN_VALUE_ERROR,
                    "out of range `value' [%.5lE < %.5lE]", value, table[0]);
        } else if (value == table[imax]) {
                *index = imax;
                return PUMAS_RETURN_SUCCESS;
        } else if (value > table[imax]) {
                *index = imax;
                return ERROR_FORMAT(PUMAS_RETURN_VALUE_ERROR,
                    "out of range `value' [%.5lE > %.5lE]", value, table[imax]);
        }

        int i1 = 0, i2 = imax;
        table_bracket(table, value, &i1, &i2);
        *index = i1;

        return PUMAS_RETURN_SUCCESS;
}

/* Low level routines: encapsulation of the tabulated CEL and DEL properties as
 * functions.
 *
 * Note that whenever a simulation context is required it is actualy optionnal
 * and can be `NULL`. The context is used for speeding up the interpolation
 * using a memory of previous indices.
 */
/**
 * Total grammage for a deterministic CEL as function of initial kinetic energy.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param scheme   The energy loss scheme.
 * @param material The index of the propagation material.
 * @param kinetic  The initial kinetic energy.
 * @return On success, the total grammage in kg/m^2 otherwise `0`.
 */
double cel_grammage(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 0)) return 0.;

        if (kinetic >= *table_get_K(physics, imax)) {
                /* Constant energy loss model. */
                const double a = *table_get_a_max(physics, material);
                const double b = *table_get_b_max(physics, scheme, material);
                const double K0 = *table_get_K(physics, imax);
                const double K1 = a / b + physics->mass;

                return *table_get_X(physics, scheme, material, imax) +
                    1. / b * log((kinetic + K1) / (K0 + K1));
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_K(physics, 0),
            table_get_X(physics, scheme, material, 0),
            table_get_X_dK(physics, scheme, material, 0),
            kinetic);
}

/**
 * Total grammage for a deterministic CEL as function of total proper time.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param scheme   The energy loss scheme.
 * @param material The index of the propagation material.
 * @param time     The total proper time variation.
 * @return On success, the total grammage in kg/m^2 otherwise `0`.
 */
double cel_grammage_as_time(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double time)
{
        const int imax = physics->n_energies - 1;
        if (time < *table_get_T(physics, scheme, material, 0)) return 0.;

        if (time >= *table_get_T(physics, scheme, material, imax)) {
                /* Constant energy loss model. */
                const double a = *table_get_a_max(physics, material);
                const double b = *table_get_b_max(physics, scheme, material);
                const double E0 = *table_get_K(physics, imax) + physics->mass;
                const double tmax =
                    *table_get_T(physics, scheme, material, imax);
                const double r =
                    E0 / (a + b * E0) * exp(a * (time - tmax) / physics->mass);
                const double kinetic = a * r / (1. - b * r) - physics->mass;
                const double K1 = a / b + physics->mass;
                return *table_get_X(physics, scheme, material, imax) +
                    1. / b * log((kinetic + K1) / (E0 - physics->mass + K1));
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_T(physics, scheme, material, 0),
            table_get_X(physics, scheme, material, 0),
            table_get_X_dT(physics, scheme, material, 0),
            time);
}

/**
 * Total proper time for a deterministic CEL in a homogeneous medium.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param scheme   The energy loss scheme.
 * @param material The index of the propagation material.
 * @param kinetic  The initial kinetic energy.
 * @return On success, the normalised proper time in kg/m^2 otherwise `0`.
 */
double cel_proper_time(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 0)) return 0.;

        if (kinetic >= *table_get_K(physics, imax)) {
                /* Constant energy loss model. */
                const double a = *table_get_a_max(physics, material);
                const double b = *table_get_b_max(physics, scheme, material);
                const double E0 = *table_get_K(physics, imax) + physics->mass;
                const double E1 = kinetic + physics->mass;

                return *table_get_T(physics, scheme, material, imax) +
                    physics->mass / a *
                    log((E1 / E0) * (a + b * E0) / (a + b * E1));
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_K(physics, 0),
            table_get_T(physics, scheme, material, 0),
            table_get_T_dK(physics, scheme, material, 0),
            kinetic);
}

/**
 * The initial kinetic energy for a given total grammage assuming a determistic
 * CEL.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param scheme   The energy loss scheme.
 * @param material The index of the propagation material.
 * @param grammage The total grammage depth.
 * @return On success, the initial kinetic energy in GeV otherwise `0`.
 */
double cel_kinetic_energy(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double grammage)
{
        const int imax = physics->n_energies - 1;
        if (grammage < *table_get_X(physics, scheme, material, 0)) return 0.;

        if (grammage >= *table_get_X(physics, scheme, material, imax)) {
                /* Constant energy loss model. */
                const double a = *table_get_a_max(physics, material);
                const double b = *table_get_b_max(physics, scheme, material);
                const double K0 = *table_get_K(physics, imax);
                const double K1 = a / b + physics->mass;

                return (K0 + K1) * exp(b * (grammage -
                                               *table_get_X(physics, scheme,
                                                   material, imax))) -
                    K1;
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_X(physics, scheme, material, 0),
            table_get_K(physics, 0),
            table_get_K_dX(physics, scheme, material, 0),
            grammage);
}

/**
 * The average CEL.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param scheme   The energy loss scheme.
 * @param material The index of the propagation material.
 * @param kinetic  The initial kinetic energy.
 * @return On success, the CEL in GeV/(kg/m^2) otherwise `0`.
 */
double cel_energy_loss(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 0)) return 0.;

        if (kinetic >= *table_get_K(physics, imax)) {
                /* Constants energy loss model. */
                return *table_get_a_max(physics, material) +
                    *table_get_b_max(physics, scheme, material) *
                    (kinetic + physics->mass);
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_K(physics, 0),
            table_get_dE(physics, scheme, material, 0),
            table_get_dE_dK(physics, scheme, material, 0), kinetic);
}

/**
 * The energy straggling.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param kinetic  The initial kinetic energy.
 * @return On success, the straggling in GeV^2/(kg/m^2) otherwise `0`.
 */
double cel_straggling(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 0)) return 0.;

        const double Kmax = *table_get_K(physics, imax);
        if (kinetic >= Kmax) {
                /* Extrapolate the straggling as E^2. */
                const double r = (kinetic + physics->mass) /
                    (Kmax + physics->mass);
                return *table_get_Omega(physics, material, imax) * r * r;
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_K(physics, 0),
            table_get_Omega(physics, material, 0),
            table_get_Omega_dK(physics, material, 0),
            kinetic);
}

/**
 * The normalised magnetic rotation angle for a deterministic CEL.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the material in which the
 *                 particle travels.
 * @param kinetic  The initial kinetic energy, in GeV.
 * @return The normalised rotation angle in kg/m^2/T.
 *
 * The magnetic rotation angle is proportional to the proper time integral.
 * Therefore it is computed from the proper time table.
 */
double cel_magnetic_rotation(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic)
{
        const int imax = physics->n_energies - 1;
        const double factor = LARMOR_FACTOR / physics->mass;
        double * const T = table_get_T(physics, PUMAS_MODE_CSDA, material, 0);
        if (kinetic <= *table_get_K(physics, 0)) return T[imax] * factor;

        if (kinetic >= *table_get_K(physics, imax)) {
                /*
                 * Neglect any magnetic rotation above the max tabulated value
                 * of the kinetic energy.
                 */
                return 0.;
        }

        /* Interpolation. */
        const double t = table_interpolate_pchip(physics, context,
            table_get_K(physics, 0), T,
            table_get_T_dK(physics, PUMAS_MODE_CSDA, material, 0),
            kinetic);

        return (T[imax] - t) * factor;
}

/**
 * The total cross section for inelastic DELs.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param kinetic  The initial kinetic energy.
 * @return On success, the DEL total cross section, otherwise `0`.
 */
double del_cross_section(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 0)) return 0.;

        if (kinetic >= *table_get_K(physics, imax)) {
                /* Constant cross section model. */
                return *table_get_CS(physics, material, imax);
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_K(physics, 0),
            table_get_CS(physics, material, 0),
            table_get_CS_dK(physics, material, 0),
            kinetic);
}

/**
 * Total number of interaction lengths for a deterministic CEL.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param kinetic  The initial kinetic energy.
 * @return On success, the number of interaction lenths, otherwise `0`.
 */
double del_interaction_length(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 0)) return 0.;

        if (kinetic >= *table_get_K(physics, imax)) {
                /* constant loss model. */
                const double k0 = *table_get_K(physics, imax);
                const double a0 = *table_get_a_max(physics, material);
                const double b0 =
                    *table_get_b_max(physics, PUMAS_MODE_MIXED, material);
                const double cs = *table_get_CS(physics, material, imax);
                const double dZ =
                    cs / b0 * log((a0 + b0 * (kinetic + physics->mass)) /
                                  (a0 + b0 * (k0 + physics->mass)));
                return *table_get_NI_in(physics, material, imax) + dZ;
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_K(physics, 0),
            table_get_NI_in(physics, material, 0),
            table_get_NI_in_dK(physics, material, 0),
            kinetic);
}

/**
 * Initial kinetic energy for a given number of interaction lengths.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param nI       The number of interaction lengths.
 * @return On success, the initial kinetic energy, otherwise `0`.
 */
double del_kinetic_from_interaction_length(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double nI)
{
        const int imax = physics->n_energies - 1;
        if (nI < *table_get_NI_in(physics, material, 0)) return 0.;

        if (nI >= *table_get_NI_in(physics, material, imax)) {
                /* constant loss model. */
                const double k0 = *table_get_K(physics, imax);
                const double a0 = *table_get_a_max(physics, material);
                const double b0 =
                    *table_get_b_max(physics, PUMAS_MODE_MIXED, material);
                const double cs = *table_get_CS(physics, material, imax);
                const double nI0 = *table_get_NI_in(physics, material, imax);
                return ((a0 + b0 * (k0 + physics->mass)) *
                               exp(b0 * (nI - nI0) / cs) -
                           a0) /
                    b0 -
                    physics->mass;
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_NI_in(physics, material, 0),
            table_get_K(physics, 0),
            table_get_K_dNI_in(physics, material, 0),
            nI);
}

/**
 * Total number of EHS interaction lengths for a deterministic CEL.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param scheme   The energy loss scheme.
 * @param material The index of the propagation material.
 * @param kinetic  The initial kinetic energy.
 * @return On success, the number of interaction lengths, otherwise `0`.
 */
double ehs_interaction_length(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double kinetic)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 0)) return 0.;

        if (kinetic >= *table_get_K(physics, imax)) {
                /* linear extrapolation. */
                const double k0 = *table_get_K(physics, imax - 1);
                const double k1 = *table_get_K(physics, imax);
                const double nI0 =
                    *table_get_NI_el(physics, scheme, material, imax - 1);
                const double nI1 =
                    *table_get_NI_el(physics, scheme, material, imax);
                return nI1 + (kinetic - k1) * (nI1 - nI0) / (k1 - k0);
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_K(physics, 0),
            table_get_NI_el(physics, scheme, material, 0),
            table_get_NI_el_dK(physics, scheme, material, 0),
            kinetic);
}

/**
 * Initial kinetic energy for a total number of EHS interaction lengths.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param scheme   The energy loss scheme.
 * @param material The index of the propagation material.
 * @param nI       The number of interaction lengths.
 * @return On success, the initial kinetic energy, otherwise `0`.
 */
double ehs_kinetic_from_interaction_length(const struct pumas_physics * physics,
    struct pumas_context * context, enum pumas_mode scheme, int material,
    double nI)
{
        const int imax = physics->n_energies - 1;
        if (nI < *table_get_NI_el(physics, scheme, material, 0)) return 0.;

        if (nI >= *table_get_NI_el(physics, scheme, material, imax)) {
                /* linear extrapolation. */
                const double k0 = *table_get_K(physics, imax - 1);
                const double k1 = *table_get_K(physics, imax);
                const double nI0 =
                    *table_get_NI_el(physics, scheme, material, imax - 1);
                const double nI1 =
                    *table_get_NI_el(physics, scheme, material, imax);
                return k1 + (nI - nI1) * (k1 - k0) / (nI1 - nI0);
        }

        /* Interpolation. */
        return table_interpolate_pchip(physics, context,
            table_get_NI_el(physics, scheme, material, 0),
            table_get_K(physics, 0),
            table_get_K_dNI_el(physics, scheme, material, 0),
            nI);
}

/*
 * Low level routines: generic and specific interpolations of various
 * tabulated data.
 */
/**
 * Interpolation of the Multiple SCattering (MSC) parameters.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The propagation material.
 * @param kinetic  The kinetic energy.
 * @param mu0      The interpolated angular cuttof for Coulomb scattering.
 * @param invlb1   The interpolated 1st transport inverse grammage.
 *
 * Interpolate the cutt-off angle for Coulomb scattering and the total 1st
 * transport path length for MSC.
 */
void table_get_msc(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic, double * mu0,
    double * invlb1)
{
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 1)) {
                *mu0 = *table_get_Mu0(physics, material, 1);
                /* Use asymptotic limit as lb1 ~ sqrt(kinetic). */
                *invlb1 = *table_get_Ms1(
                    physics, context->mode.energy_loss, material, 1) *
                    sqrt((*table_get_K(physics, 1)) / kinetic);
        } else if (kinetic >= *table_get_K(physics, imax)) {
                *mu0 = *table_get_Mu0(physics, material, imax);
                /* Use asymptotic limit as lb1 ~ kinetic. */
                *invlb1 = *table_get_Ms1(
                    physics, context->mode.energy_loss, material, imax) *
                    (*table_get_K(physics, imax)) / kinetic;
        } else {
                const double * x = table_get_K(physics, 0);
                const int i1 = table_index(physics, NULL, x, kinetic);
                x += i1;
                const double * y0 = table_get_Mu0(physics, material, i1);
                const double * m0 = table_get_Mu0_dK(physics, material, i1);
                const double * y1 = table_get_Ms1(
                    physics, context->mode.energy_loss, material, i1);
                const double * m1 = table_get_Ms1_dK(
                    physics, context->mode.energy_loss, material, i1);
                const double dx = x[1] - x[0];
                const double t = (kinetic - x[0]) / dx;
                *mu0 = math_pchip_interpolate(
                    t, y0[0], y0[1], m0[0] * dx, m0[1] * dx);
                *invlb1 = math_pchip_interpolate(
                    t, y1[0], y1[1], m1[0] * dx, m1[1] * dx);
        }
}

/* Hermite polynomials interpolation using the 1st derivative.
 *
 * Reference:
 *   https://fr.wikipedia.org/wiki/Spline_cubique_d%27Hermite
 */
double math_pchip_interpolate(
    double t, double p0, double p1, double m0, double m1)
{
        const double c2 = -3 * (p0 - p1) - 2 * m0 - m1;
        const double c3 = 2 * (p0 - p1) + m0 + m1;

        return p0 + t * (m0 + t * (c2 + t * c3));
}

/**
 * Piecewise Hermite polynomial difference using a finite difference.
 *
 * @param Physics Handle for physics tables.
 * @param context The simulation context.
 * @param table_X Table of x values.
 * @param table_Y Table of y values.
 * @param table_M Table of dy/dx values.
 * @param x       Point at which the interpolant is evaluated.
 * @return The interpolated value.
 *
 * Compute a cubic interpolant of property Y at x given table_Y values as
 * table_X values and the finite difference table_M. Note that table_X must be
 * strictly monotone but not necessarily with a constant stepping. The
 * `context` parameter is used for mapping the index of x in table_X from a
 * memory. It can be `NULL`, though providing a `context` leads to a speed up
 * on average for Monte-Carlo stepping.
 * **Warning** : there is no bound check. x must be in the range of table_X.
 */
double table_interpolate_pchip(const struct pumas_physics * physics,
    struct pumas_context * context, const double * table_X,
    const double * table_Y, const double * table_M, double x)
{
        const int i1 = table_index(physics, context, table_X, x);
        const int i2 = i1 + 1;
        const double dX = table_X[i2] - table_X[i1];
        const double t = (x - table_X[i1]) / dX;
        const double m1 = table_M[i1] * dX;
        const double m2 = (i2 > 1) ? table_M[i2] * dX : m1;
        return math_pchip_interpolate(t, table_Y[i1], table_Y[i2], m1, m2);
}

/**
 * Find the index closest to `value`, from below.
 *
 * @param Physics Handle for physics tables.
 * @param context The simulation context.
 * @param table   The tabulated values.
 * @param value   The value to bracket.
 * @return In case of success the closest index from below is returned,
 * otherwise -1 in case of underflow or `pumas_physics::n_energies-1` in
 * case of overflow.
 *
 * Compute the table index for the given entry `value` using a dichotomy
 * algorithm. If a `context` is not `NULL`, `value` is checked against the
 * last used indices in the table, before doing the dichotomy search.
 */
int table_index(const struct pumas_physics * physics,
    struct pumas_context * context, const double * table, double value)
{
        int * last = NULL;
        if (context != NULL) {
                /* Check if the last used indices are still relevant. */
                struct simulation_context * const ctx =
                    (struct simulation_context * const)context;
                if (table == table_get_K(physics, 0))
                        last = ctx->index_K_last;
                else
                        last = ctx->index_X_last;

                if ((value >= table[last[0]]) && (value < table[last[0] + 1]))
                        return last[0];
                if ((value >= table[last[1]]) && (value < table[last[1] + 1]))
                        return last[1];
        }

        /* Check the boundaries. */
        const int imax = physics->n_energies - 1;
        if (value < table[0]) return -1;
        if (value >= table[imax]) return imax;

        /* Bracket the value. */
        int i1 = 0, i2 = imax;
        table_bracket(table, value, &i1, &i2);

        if (context != NULL) {
                /* Update the last used indices. */
                if (i1 != last[0])
                        last[0] = i1;
                else if (i1 != last[1])
                        last[1] = i1;
        }
        return i1;
}

/**
 * Recursive bracketing of a table value.
 *
 * @param table The tabulated values.
 * @param value The value to bracket.
 * @param p1 The lower bracketing index.
 * @param p2 The upper bracketing index.
 * @return At final return p1 points to the closest from below index
 * to `value` and p2 to the closest from above.
 *
 * Refine the bracketing indices [p1, p2] of `value` in table using a
 * recursive procedure.
 */
void table_bracket(const double * table, double value, int * p1, int * p2)
{
        int i3 = (*p1 + *p2) / 2;
        if (value >= table[i3])
                *p1 = i3;
        else
                *p2 = i3;
        if (*p2 - *p1 >= 2) table_bracket(table, value, p1, p2);
}

/** Float version of the index bracketing */
static void table_bracketf(float * table, float value, int * p1, int * p2)
{
        int i3 = (*p1 + *p2) / 2;
        if (value >= table[i3])
                *p1 = i3;
        else
                *p2 = i3;
        if (*p2 - *p1 >= 2) table_bracketf(table, value, p1, p2);
}

/*
 * Low level routines: inlined encapsulations of table accesses.
 *
 * The tables are stored linearly in the shared data segment, allocated on the
 * heap. The following routines provide pointers on the corresponding table
 * elements.
 */
/**
 * Encapsulation of the kinetic energy table.
 *
 * @param Physics  Handle for physics tables.
 * @param row The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_K(const struct pumas_physics * physics, int row)
{
        return physics->table_K + row;
}

double * table_get_K_dX(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_K_dX +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

double * table_get_K_dNI_el(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_K_dNI_el +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

double * table_get_K_dNI_in(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_K_dNI_in + material * physics->n_energies + row;
}

/**
 * Encapsulation of the total grammage table.
 *
 * @param Physics  Handle for physics tables.
 * @param scheme   The energy loss scheme.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_X(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_X +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

double * table_get_X_dK(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_X_dK +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

double * table_get_X_dT(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_X_dT +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

/**
 * Encapsulation of the proper time table.
 *
 * @param Physics  Handle for physics tables.
 * @param scheme   The energy loss scheme.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_T(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_T +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

double * table_get_T_dK(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_T_dK +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

/**
 * Encapsulation of the dE/dX table.
 *
 * @param Physics  Handle for physics tables.
 * @param scheme   The energy loss scheme.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_dE(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_dE +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

double * table_get_dE_dK(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_dE_dK +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

/**
 * Encapsulation of the energy straggling table.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_Omega(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_Omega + material * physics->n_energies + row;
}

double * table_get_Omega_dK(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_Omega_dK + material * physics->n_energies + row;
}

/**
 * Encapsulation of the number of EHS interaction lengths.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_NI_el(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme >= PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED :
                                                   PUMAS_MODE_CSDA;
        return physics->table_NI_el +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

double * table_get_NI_el_dK(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme >= PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED :
                                                   PUMAS_MODE_CSDA;
        return physics->table_NI_el_dK +
            (scheme * physics->n_materials + material) * physics->n_energies +
            row;
}

/**
 * Encapsulation of the number of interaction lengths for inelastic DELs.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_NI_in(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_NI_in + material * physics->n_energies + row;
}

double * table_get_NI_in_dK(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_NI_in_dK + material * physics->n_energies + row;
}

/**
 * Encapsulation of the cross section table, for inelastic DELs.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_CS(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_CS + material * physics->n_energies + row;
}

double * table_get_CS_dK(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_CS_dK + material * physics->n_energies + row;
}

/**
 * Encapsulation of the fractional cross-sections table, for inelastic DELs.
 *
 * @param Physics   Handle for physics tables.
 * @param process   The process index.
 * @param component The component index.
 * @param row       The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_CSf(
    const struct pumas_physics * physics, int process, int component, int row)
{
        return physics->table_CSf +
            (process * physics->n_components + component) *
            physics->n_energies +
            row;
}

double * table_get_CSf_dK(
    const struct pumas_physics * physics, int process, int component, int row)
{
        return physics->table_CSf_dK +
            (process * physics->n_components + component) *
            physics->n_energies +
            row;
}

/**
 * Encapsulation of the cross-sections normalisation table.
 *
 * @param Physics Handle for physics tables.
 * @param process The process index.
 * @param element The element index.
 * @param row     The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_CSn(
    const struct pumas_physics * physics, int process, int element, int row)
{
        return physics->table_CSn +
            (process * physics->n_elements + element) * physics->n_energies +
            row;
}

double * table_get_CSn_dK(
    const struct pumas_physics * physics, int process, int element, int row)
{
        return physics->table_CSn_dK +
            (process * physics->n_elements + element) * physics->n_energies +
            row;
}

/**
 * Encapsulation of the fractional lower threshold for inelastic DELs.
 *
 * @param Physics Handle for physics tables.
 * @param process The process index.
 * @param element The element index.
 * @param row     The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_Xt(
    const struct pumas_physics * physics, int process, int element, int row)
{
        return physics->table_Xt +
            (process * physics->n_elements + element) * physics->n_energies +
            row;
}

/**
 * Encapsulation of the lower kinetic threshold for inelatic DELs.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @return A pointer to the table element.
 */
double * table_get_Kt(const struct pumas_physics * physics, int material)
{
        return physics->table_Kt + material;
}

/**
 * Encapsulation of the temporary average CEL table, element wise.
 *
 * @param Physics  Handle for physics tables.
 * @param process  The process index.
 * @param element  The element index.
 * @param row      The kinetic energy row index.
 * @param table    The temporary table.
 * @return A pointer to the table element.
 */
double * table_get_cel(const struct pumas_physics * physics, int process,
    int element, int row, double * table)
{
        return table +
            (process * physics->n_elements + element) * physics->n_energies +
            row;
}

/**
 * Encapsulation of the temporary energy straggling table, element wise.
 *
 * @param Physics  Handle for physics tables.
 * @param process  The process index.
 * @param element  The element index.
 * @param row      The kinetic energy row index.
 * @param table    The temporary table.
 * @return A pointer to the table element.
 */
double * table_get_stg(const struct pumas_physics * physics, int process,
    int element, int row, double * table)
{
        return table +
            ((N_DEL_PROCESSES + process) * physics->n_elements + element) *
            physics->n_energies + row;
}

/**
 * Encapsulation of the magnetic deflection table, when using CSDA in a
 * homogeneous medium of infinite extension.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @param order    The order of the Taylor expansion.
 * @param row      The kinetic row index.
 * @return A pointer to the table element.
 */
double * table_get_Li(
    const struct pumas_physics * physics, int material, int order, int row)
{
        return physics->table_Li +
            (material * N_LARMOR_ORDERS + order) * physics->n_energies + row;
}

double * table_get_Li_dK(
    const struct pumas_physics * physics, int material, int order, int row)
{
        return physics->table_Li_dK +
            (material * N_LARMOR_ORDERS + order) * physics->n_energies + row;
}

/**
 * Encapsulation of the last tabulated ionisation dE/dX.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @return A pointer to the table element.
 */
double * table_get_a_max(const struct pumas_physics * physics, int material)
{
        return physics->table_a_max + material;
}

/**
 * Encapsulation of the last tabulated radiative dE/dX parameter.
 *
 * @param Physics  Handle for physics tables.
 * @param scheme   The energy loss scheme.
 * @param material The material index.
 * @return A pointer to the table element.
 */
double * table_get_b_max(
    const struct pumas_physics * physics, int scheme, int material)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_b_max + scheme * physics->n_materials + material;
}

/**
 * Encapsulation of the angular cutoff for Coulomb scattering.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_Mu0(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_Mu0 + material * physics->n_energies + row;
}

double * table_get_Mu0_dK(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_Mu0_dK + material * physics->n_energies + row;
}

/**
 * Encapsulation of the interaction length for EHS events.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_Lb(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_Lb + material * physics->n_energies + row;
}

double * table_get_Lb_dK(
    const struct pumas_physics * physics, int material, int row)
{
        return physics->table_Lb_dK + material * physics->n_energies + row;
}

/*!
 * Encapsulation of the Multiple SCattering (MSC) 1st transport path length.
 *
 * @param Physics  Handle for physics tables.
 * @param scheme   The energy loss scheme.
 * @param material The material index.
 * @param row      The kinetic energy row index.
 * @return A pointer to the table element.
 */
double * table_get_Ms1(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_Ms1 +
            ((scheme + 1) * physics->n_materials + material) *
            physics->n_energies + row;
}

double * table_get_Ms1_dK(
    const struct pumas_physics * physics, int scheme, int material, int row)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return physics->table_Ms1_dK +
            ((scheme + 1) * physics->n_materials + material) *
            physics->n_energies + row;
}

/*!
 * Encapsulation of the temporary MSC 1st transport path length, element wise.
 *
 * @param Physics  Handle for physics tables.
 * @param scheme   The energy loss scheme.
 * @param element  The material index.
 * @param row      The kinetic energy row index.
 * @param table    The temporary table.
 * @return A pointer to the table element.
 */
double * table_get_ms1(const struct pumas_physics * physics, int scheme,
    int element, int row, double * table)
{
        scheme = (scheme > PUMAS_MODE_MIXED) ? PUMAS_MODE_MIXED : scheme;
        return table + (scheme * physics->n_elements + element) *
            physics->n_energies + row;
}

/**
 * Encapsulation of the tabulated DCS data.
 *
 * @param Physics Handle for physics tables.
 * @param element The element index.
 * @param process The process index.
 * @param row     The kinetic energy row index.
 * @return A pointer to the table element.
 */
float * table_get_dcs(const struct pumas_physics * physics,
    int process, int element, int kinetic)
{
        return physics->table_DCS + physics->n_table_dcs * 2 * (
            (process * physics->n_elements + element) * physics->n_energies +
            kinetic);
}

/**
 * Encapsulation of the tabulated DCS envelope.
 *
 * @param Physics Handle for physics tables.
 * @param element The element index.
 * @param process The process index.
 * @param row     The kinetic energy row index.
 * @return A pointer to the table element.
 */
float * table_get_dcs_envelope(const struct pumas_physics * physics,
    int process, int element, int kinetic)
{
        return physics->table_DCS_envelope + 2 * (
            (process * physics->n_elements + element) * physics->n_energies +
            kinetic);
}

/*
 * Low level routines: propagation.
 */
/**
 * CSDA propagation routine for a uniform and infinite medium.
 *
 * @param Physics      Handle for physics tables.
 * @param context      The simulation context.
 * @param state        The initial/final state.
 * @param medium_index The index of the propagation medium.
 * @param locals       Handle for the local properties of the uniform medium.
 * @param error_       The error data.
 * @return The end condition event.
 *
 * Transport the particle through a uniform medium using the CSDA. At output
 * the final kinetic energy, the effective distance traveled and the
 * proper time spent are updated.
 *
 * Backward propagation allows to compute the required minimum kinetic
 * energy for crossing the medium. The corresponding distance and
 * proper time are also given.
 * **Warning** : the initial state must have been initialized before the call.
 */
enum pumas_event transport_with_csda(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state,
    struct pumas_medium * medium, struct medium_locals * locals,
    struct error_context * error_)
{
        /* Unpack and backup the initial state. */
        const int material = medium->material;
        const double density = locals->api.density;
        const double ki = state->energy;
        const double di = state->distance;
        const double ti = state->time;
        const double xi =
            cel_grammage(physics, context, PUMAS_MODE_CSDA, material, ki);
        const double Ti =
            cel_proper_time(physics, context, PUMAS_MODE_CSDA, material, ki);

        /* Register the start of the the track, if recording. */
        enum pumas_event event = PUMAS_EVENT_NONE;
        struct pumas_recorder * recorder = context->recorder;
        const int record = (recorder != NULL);
        if (record)
                record_state(context, medium, event | PUMAS_EVENT_START, state);

        /* Get the end point with CSDA. */
        double xB;
        if (context->event & PUMAS_EVENT_LIMIT_GRAMMAGE) {
                xB = context->limit.grammage - state->grammage;
                event = PUMAS_EVENT_LIMIT_GRAMMAGE;
        } else
                xB = DBL_MAX;
        if (context->event & PUMAS_EVENT_LIMIT_DISTANCE) {
                const double xD = density * (context->limit.distance - di);
                if (xD < xB) {
                        xB = xD;
                        event = PUMAS_EVENT_LIMIT_DISTANCE;
                }
        }
        int decayed = 0;
        double time_max = (context->event & PUMAS_EVENT_LIMIT_TIME) ?
            context->limit.time : 0.;
        if (context->mode.decay == PUMAS_MODE_RANDOMISED) {
                struct simulation_context * c =
                    (struct simulation_context *)context;
                if ((time_max <= 0.) || (c->lifetime < time_max)) {
                        time_max = c->lifetime;
                        decayed = 1;
                }
        }
        if (time_max > 0.) {
                const double dt = time_max - ti;
                if (dt <= 0.) {
                        xB = 0.;
                        event = PUMAS_EVENT_LIMIT_TIME;
                } else {
                        const double sgn =
                            (context->mode.direction == PUMAS_MODE_FORWARD) ?
                            1. : -1.;
                        const double Tf = Ti - sgn * dt * density;
                        if (Tf > 0.) {
                                const double xT = fabs(
                                    xi - cel_grammage_as_time(physics, context,
                                             PUMAS_MODE_CSDA, material, Tf));
                                if (xT < xB) {
                                        xB = xT;
                                        event = PUMAS_EVENT_LIMIT_TIME;
                                }
                        }
                }
        }
        if (xB <= 0.) return event;

        double xf, kf;
        if (context->mode.direction == PUMAS_MODE_FORWARD) {
                if (xi > xB) {
                        xf = xi - xB;
                        if (xB < EPSILON_X * xi) {
                                kf = ki - cel_energy_loss(physics, context,
                                    PUMAS_MODE_CSDA, material, ki) * xB;
                                if (kf < 0.) kf = 0.;
                        } else {
                                kf = cel_kinetic_energy(physics, context,
                                    PUMAS_MODE_CSDA, material, xf);
                        }
                } else {
                        xf = kf = 0.;
                        event = PUMAS_EVENT_LIMIT_ENERGY;
                }

                if (context->event & PUMAS_EVENT_LIMIT_ENERGY) {
                        if (ki <= context->limit.energy)
                                return PUMAS_EVENT_LIMIT_ENERGY;
                        if (kf < context->limit.energy) {
                                kf = context->limit.energy;
                                xf = cel_grammage(physics, context,
                                    PUMAS_MODE_CSDA, material, kf);
                                event = PUMAS_EVENT_LIMIT_ENERGY;
                        }
                }
        } else {
                if (xB == DBL_MAX) {
                        xf = xB;
                        kf = DBL_MAX;
                } else {
                        xf = xB + xi;
                        if (xB < EPSILON_X * xi) {
                                kf = ki + cel_energy_loss(physics, context,
                                    PUMAS_MODE_CSDA, material, ki) * xB;
                        } else {
                                kf = cel_kinetic_energy(physics, context,
                                    PUMAS_MODE_CSDA, material, xf);
                        }
                }
                if (context->event & PUMAS_EVENT_LIMIT_ENERGY) {
                        if (ki >= context->limit.energy)
                                return PUMAS_EVENT_LIMIT_ENERGY;
                        if (kf > context->limit.energy) {
                                kf = context->limit.energy;
                                xf = cel_grammage(physics, context,
                                    PUMAS_MODE_CSDA, material, kf);
                                event = PUMAS_EVENT_LIMIT_ENERGY;
                        }
                }
        }

        /* Update the end point statistics. */
        const double distance = fabs(xf - xi) / density;
        state->energy = kf;
        if (event == PUMAS_EVENT_LIMIT_DISTANCE)
                state->distance = context->limit.distance;
        else
                state->distance += distance;
        if (event == PUMAS_EVENT_LIMIT_GRAMMAGE)
                state->grammage = context->limit.grammage;
        else
                state->grammage += distance * density;
        if (event == PUMAS_EVENT_LIMIT_TIME) {
                state->time = time_max;
                state->decayed = decayed;
                if (decayed) event = PUMAS_EVENT_VERTEX_DECAY;
        } else
                state->time += fabs(Ti - cel_proper_time(physics, context,
                                             PUMAS_MODE_CSDA, material, kf)) /
                    density;
        if (context->mode.direction == PUMAS_MODE_BACKWARD)
                state->weight *= cel_energy_loss(physics, context,
                                     PUMAS_MODE_CSDA, material, kf) /
                    cel_energy_loss(physics, context, PUMAS_MODE_CSDA,
                                     material, ki);
        if (context->mode.decay == PUMAS_MODE_WEIGHTED)
                state->weight *= exp(-fabs(ti - state->time) / physics->ctau);

        /* Update the position and direction. */
        if ((locals->magnetized != 0)) {
                if (transport_csda_deflect(physics, context, state, medium,
                        locals, ki, distance, error_) != PUMAS_RETURN_SUCCESS)
                        return event;
        } else {
                double path;
                if (context->mode.direction == PUMAS_MODE_FORWARD)
                        path = distance;
                else
                        path = -distance;
                state->position[0] += path * state->direction[0];
                state->position[1] += path * state->direction[1];
                state->position[2] += path * state->direction[2];
        }

        /* Register the end of the track, if recording. */
        if (record)
                record_state(context, medium, event | PUMAS_EVENT_STOP, state);

        return event;
}

/**
 * Apply the magnetic deflection in CSDA scheme and uniform medium.
 *
 * @param Physics      Handle for physics tables.
 * @param context      The simulation context.
 * @param state        The final state.
 * @param medium       The propagation medium.
 * @param locals       Handle for the local properties of the uniform medium.
 * @param ki           The initial kinetic energy.
 * @param distance     The travelled distance.
 * @param error_       The error data.
 * @return #PUMAS_RETURN_SUCCESS on success, #PUMAS_ERROR otherwise.
 *
 * Compute the magnetic deflection between two kinetic energies using the CSDA.
 * The final kinetic energy is read from the state. At return the final state
 * position and direction are updated.
 */
enum pumas_return transport_csda_deflect(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state,
    struct pumas_medium * medium, struct medium_locals * locals, double ki,
    double distance, struct error_context * error_)
{
        /* Unpack arguments */
        const double charge = state->charge;
        const double kf = state->energy;
        double * const position = state->position;
        double * const direction = state->direction;
        const int material = medium->material;
        const double density = locals->api.density;
        const double * const magnet = locals->api.magnet;

        /* Compute the local basis and the transverse magnetic field
         * amplitude. Note that ex, ey and ez are not normed but already
         * multiplied by the cos(theta) and sin(theta) factors.
         */
        double b0, ex[3], ey[3], ez[3];
        {
                b0 = sqrt(magnet[0] * magnet[0] + magnet[1] * magnet[1] +
                    magnet[2] * magnet[2]);
                double b0_i = 1.0 / b0;

                /* Negative charge convention for particles. */
                ey[0] = (direction[2] * magnet[1] - direction[1] * magnet[2]) *
                    b0_i;
                ey[1] = (direction[0] * magnet[2] - direction[2] * magnet[0]) *
                    b0_i;
                ey[2] = (direction[1] * magnet[0] - direction[0] * magnet[1]) *
                    b0_i;

                {
                        double d = b0_i * b0_i * (magnet[0] * direction[0] +
                                                     magnet[1] * direction[1] +
                                                     magnet[2] * direction[2]);
                        ez[0] = magnet[0] * d;
                        ez[1] = magnet[1] * d;
                        ez[2] = magnet[2] * d;
                }

                ex[0] = direction[0] - ez[0];
                ex[1] = direction[1] - ez[1];
                ex[2] = direction[2] - ez[2];
        }

        /* Update the position and direction. */
        double dx, dy, dz, dp;
        {
                /* Negative charge convention. */
                double a = -b0 * charge / density;
                double pi =
                    a * cel_magnetic_rotation(physics, context, material, ki);
                double pf =
                    a * cel_magnetic_rotation(physics, context, material, kf);
                dp = pf - pi;

                const double ps = fabs(pi + pf);
                if (ps == 0.) return PUMAS_RETURN_SUCCESS;
                const double dp0 = fabs(dp);
                if ((dp0 < 1E-02) && (dp0 / ps < 1E-02)) {
                        /* Use the leading order expressions for a small
                         * rotation varying linearly with the distance.
                         */
                        if (ki >= kf)
                                dz = distance;
                        else
                                dz = -distance;

                        const double sixth = 1 / 6.0;
                        dx = dz * (1.0 - sixth * dp * dp);
                        dy = 0.5 * dz * dp;
                } else {
                        double xi, yi, zi, xf, yf, zf;
                        if (csda_magnetic_transport(physics, context, material,
                                density, b0, charge, ki, pi, &xi, &yi, &zi,
                                error_) != PUMAS_RETURN_SUCCESS)
                                return error_->code;
                        if (csda_magnetic_transport(physics, context, material,
                                density, b0, charge, kf, pf, &xf, &yf, &zf,
                                error_) != PUMAS_RETURN_SUCCESS)
                                return error_->code;

                        /* Rotate back to the initial frame. */
                        {
                                const double si = sin(pi);
                                const double ci = cos(pi);
                                xf -= xi;
                                yf -= yi;
                                dx = ci * xf + si * yf;
                                dy = -si * xf + ci * yf;
                        }
                        dz = zf - zi;
                }
        }
        position[0] += dx * ex[0] + dy * ey[0] + dz * ez[0];
        position[1] += dx * ex[1] + dy * ey[1] + dz * ez[1];
        position[2] += dx * ex[2] + dy * ey[2] + dz * ez[2];

        const double s = sin(dp);
        const double c = cos(dp);
        direction[0] = c * ex[0] + s * ey[0] + ez[0];
        direction[1] = c * ex[1] + s * ey[1] + ez[1];
        direction[2] = c * ex[2] + s * ey[2] + ez[2];

        return PUMAS_RETURN_SUCCESS;
}

/**
 * Compute the total magnetic transport within CSDA, for a uniform medium.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param density  The material density.
 * @param magnet   The transverse magnetic field amplitude.
 * @param charge   The particle charge.
 * @param kinetic  The kinetic energy.
 * @param phase    The magnetic angular rotation.
 * @param x        The total magnetic transport for x coordinate.
 * @param y        The total magnetic transport for y coordinate.
 * @param z        The total magnetic transport for z coordinate.
 * @param error_   The error data.
 * @return `PUMAS_RETURN_SUCCESS` on success or `PUMAS_ERROR` otherwise, i.e. if
 * an
 * out of bound error ooccured.
 */
enum pumas_return csda_magnetic_transport(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double density, double magnet,
    double charge, double kinetic, double phase, double * x, double * y,
    double * z, struct error_context * error_)
{
        const int imax = physics->n_energies - 1;
        double k0 = kinetic;
        int i1, i2;
        if (kinetic <= *table_get_K(physics, 0)) {
                k0 = *table_get_K(physics, 0);
                i1 = 0;
                i2 = 1;
        } else if (kinetic >= *table_get_K(physics, imax)) {
                /* Neglect deflection at very high energy. */
                *y = 0.0;
                *x = *z = -(cel_grammage(physics, context, PUMAS_MODE_CSDA,
                                material, kinetic) -
                    *table_get_X(physics, 0, material, imax));
                return PUMAS_RETURN_SUCCESS;
        } else {
                /* Interpolation of the step starting energy. */
                i1 = table_index(physics, context, table_get_K(physics, 0), k0);
                i2 = i1 + 1;
        }

        /* Check that the phase does not exceed the sine & cosine
         * interpolation range.
         */
        const double max_phi = 2.0 * M_PI;
        const double poly_x[N_LARMOR_ORDERS] = { 1.000000000e+000,
                0.000000000e+000, -5.000000000e-001, -1.048969541e-002,
                5.597396438e-002, -6.401353612e-003, -4.495849930e-004,
                6.583532135e-005 };
        const double poly_y[N_LARMOR_ORDERS] = { 0.000000000e+000,
                1.000000000e+000, -1.400755143e-002, -1.350173383e-001,
                -2.838778336e-002, 2.123237056e-002, -3.094290091e-003,
                1.409012754e-004 };

        if (fabs(phase) > max_phi)
                return ERROR_VREGISTER(PUMAS_RETURN_VALUE_ERROR,
                    "magnetic rotation is too strong [%.5lE > %.5lE]",
                    fabs(phase), max_phi);

        /* Compute the local x, y and z. */
        double x1, x2, y1, y2, z1, z2;
        {
                /* Negative charge convention. */
                double a = -magnet * charge / density;
                x1 = x2 = y1 = y2 = 0.;
                double u = 1.;
                int j;
                for (j = 0; j < N_LARMOR_ORDERS; j++) {
                        x1 += u * poly_x[j] *
                            (*table_get_Li(physics, material, j, i1));
                        y1 += u * poly_y[j] *
                            (*table_get_Li(physics, material, j, i1));
                        x2 += u * poly_x[j] *
                            (*table_get_Li(physics, material, j, i2));
                        y2 += u * poly_y[j] *
                            (*table_get_Li(physics, material, j, i2));
                        u *= a;
                }
                z1 = *table_get_Li(physics, material, 0, i1);
                z2 = *table_get_Li(physics, material, 0, i2);
        }

        /* Update the position. */
        {
                double h = (k0 - *table_get_K(physics, i1)) /
                    (*table_get_K(physics, i2) - *table_get_K(physics, i1));
                *x = (x1 + h * (x2 - x1)) / density;
                *y = (y1 + h * (y2 - y1)) / density;
                *z = (z1 + h * (z2 - z1)) / density;
        }

        return PUMAS_RETURN_SUCCESS;
}

/**
 * Propagation in arbitrary media.
 *
 * @param Physics         Handle for physics tables.
 * @param context         The simulation context.
 * @param state           The initial/final state.
 * @param medium_ptr      The initial/final propagation medium.
 * @param locals          Handle for the local properties of the starting
 *                        medium.
 * @param error           The error data.
 * @param step_max_medium The step limitation from the medium.
 * @param step_max_type   The type of geometry step (exact or approximate).
 * @param step_max_locals The step limitation from the local properties.
 * @return The end condition event.
 *
 * Transport through a set of media described by a medium callback. At output
 * the final kinetic energy, the total distance travelled and the total proper
 * time spent are updated.
 *
 * **Warning** : The initial state must have been initialized before the call.
 */
enum pumas_event transport_with_stepping(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state,
    struct pumas_medium ** medium_ptr, struct medium_locals * locals,
    double step_max_medium, enum pumas_step step_max_type,
    double step_max_locals, struct error_context * error_)
{
        /* Check the config */
        if ((context->random == NULL) &&
            ((context->mode.energy_loss > PUMAS_MODE_CSDA) ||
                (context->mode.scattering == PUMAS_MODE_MIXED) ||
                (context->mode.decay == PUMAS_MODE_RANDOMISED))) {
                ERROR_REGISTER(
                    PUMAS_RETURN_MISSING_RANDOM, "no random engine provided");
                return PUMAS_EVENT_NONE;
        }

        /* Unpack data. */
        struct pumas_medium * medium = *medium_ptr;
        int material = medium->material;

        /* Check for a straight path in a uniform medium */
        const enum pumas_mode scheme = context->mode.energy_loss;
        int straight =
            ((context->mode.scattering == PUMAS_MODE_DISABLED) &&
            (scheme <= PUMAS_MODE_MIXED) &&
            (step_max_locals <= 0.) && !locals->magnetized) ?
            1 :
            0;

        /* Register the start of the the track, if recording. */
        struct simulation_context * const context_ =
            (struct simulation_context *)context;
        context_->step_event = PUMAS_EVENT_NONE;
        struct pumas_recorder * recorder = context->recorder;
        const int record = (recorder != NULL);
        if (record)
                record_state(context, medium,
                    context_->step_event | PUMAS_EVENT_START, state);

        /* Initialise some temporary data for the propagation, weights, ect ...
         */
        double ti = state->time;
        double wi = state->weight;
        double Xi = state->grammage;
        double dei, Xf;
        if (scheme > PUMAS_MODE_DISABLED) {
                const double ki = state->energy;
                Xf = cel_grammage(physics, context, scheme, material, ki);
                dei = 1. /
                    cel_energy_loss(physics, context, scheme, material, ki);

        } else {
                Xf = dei = 0.;
        }

        /* Check for any initial violation of external limits. */
        if ((context->event & PUMAS_EVENT_LIMIT_DISTANCE) &&
            (state->distance >= context->limit.distance))
                return PUMAS_EVENT_LIMIT_DISTANCE;
        if ((context->event & PUMAS_EVENT_LIMIT_GRAMMAGE) &&
            (state->grammage >= context->limit.grammage))
                return PUMAS_EVENT_LIMIT_GRAMMAGE;
        if ((context->event & PUMAS_EVENT_LIMIT_TIME) &&
            (state->time >= context->limit.time))
                return PUMAS_EVENT_LIMIT_TIME;
        if (state->weight <= 0.) return PUMAS_EVENT_WEIGHT;

        /* Initialise the stepping data. */
        double grammage_max;
        context_->step_event = PUMAS_EVENT_NONE;
        context_->step_first = 1;
        context_->step_X_limit = (context->event & PUMAS_EVENT_LIMIT_ENERGY) ?
            cel_grammage(physics, context,
                (scheme > PUMAS_MODE_DISABLED) ? scheme : PUMAS_MODE_CSDA,
                material, context->limit.energy) :
            0.;
        context_->step_invlb1 = 0;
        transport_limit(
            physics, context, state, material, Xi, Xf, &grammage_max);
        if (context_->step_event) return context_->step_event;

        /* Step through the media. */
        int step_index = 1;
        for (;;) {
                /* Do a transportation step. */
                if (step_index > 1) {
                        /* Update the geometric step length */
                        step_max_type = context->medium(
                            context, state, NULL, &step_max_medium);
                        if ((step_max_medium > 0.) &&
                            (step_max_type == PUMAS_STEP_CHECK))
                                step_max_medium += 0.5 * STEP_MIN;
                }

                struct pumas_medium * new_medium = NULL;
                if (step_transport(physics, context, state, straight, medium,
                        locals, grammage_max, step_max_medium, step_max_type,
                        &step_max_locals, &new_medium, error_)
                        != PUMAS_RETURN_SUCCESS)
                        return context_->step_event;
                step_index++;

                /* Check for any event. */
                int record_step;
                if ((record != 0) && (recorder->period > 0) &&
                    (step_index != 1)) {
                        record_step = (step_index % recorder->period) == 0;
                } else {
                        record_step = 0;
                }
                if (!context_->step_event && !record_step) continue;

                /* Update the weight if a boundary or hard energy loss
                 * occured.
                 */
                if ((context->mode.direction == PUMAS_MODE_BACKWARD) &&
                    (context->mode.energy_loss >= PUMAS_MODE_CSDA)) {
                        const double wt =
                            (context->mode.decay == PUMAS_MODE_WEIGHTED) ?
                            exp(-fabs(state->time - ti) / physics->ctau) :
                            1.;
                        state->weight = wi * wt * dei *
                            cel_energy_loss(physics, context, scheme,
                                material, state->energy);
                } else if (context->mode.decay == PUMAS_MODE_WEIGHTED) {
                        state->weight =
                            wi * exp(-fabs(state->time - ti) / physics->ctau);
                }

                /* Process the event. */
                if (!context_->step_event) {
                        /* Register the current state. */
                        record_state(
                            context, medium, context_->step_event, state);
                } else {
                        if (context_->step_event &
                            (PUMAS_EVENT_LIMIT | PUMAS_EVENT_WEIGHT |
                                PUMAS_EVENT_VERTEX_DECAY)) {
                                /* A boundary was reached. Let's stop the
                                 * simulation.
                                 */
                                break;
                        } else if (context_->step_event &
                            (PUMAS_EVENT_VERTEX_DEL |
                                       PUMAS_EVENT_VERTEX_COULOMB)) {
                                /* A discrete process occured. */
                                if (context_->step_event &
                                    PUMAS_EVENT_VERTEX_DEL) {
                                        /* Backup the pre step point if
                                         * recording.
                                         */
                                        const int rec =
                                            record && (recorder->period > 0);
                                        double ki, ui[3];
                                        if (rec != 0) {
                                                ki = state->energy;
                                                memcpy(ui, state->direction,
                                                    sizeof(ui));
                                        }

                                        /* Apply the inelastic DEL. */
                                        transport_do_del(
                                            physics, context, state, material);

                                        /* Record the pre step point. */
                                        if (rec != 0) {
                                                const double kf =
                                                    state->energy;
                                                double uf[3];
                                                memcpy(uf, state->direction,
                                                    sizeof(uf));
                                                state->energy = ki;
                                                memcpy(state->direction, ui,
                                                    sizeof(state->direction));
                                                record_state(context, medium,
                                                    context_->step_event,
                                                    state);
                                                state->energy = kf;
                                                memcpy(state->direction, uf,
                                                    sizeof(state->direction));
                                        }

                                        /* Check for any stop condition */
                                        if ((context->event &
                                             context_->step_event) ||
                                             context_->step_event ==
                                             PUMAS_EVENT_WEIGHT)
                                                break;

                                        /* Record the post step point. */
                                        if (rec != 0)
                                                record_state(context, medium,
                                                    PUMAS_EVENT_NONE, state);

                                        /* Reset the stepping data memory since
                                         * the kinetic energy has changed.
                                         */
                                        context_->step_first = 1;
                                        context_->step_invlb1 = 0;
                                } else {
                                        /* An EHS event occured. */
                                        transport_do_ehs(
                                            physics, context, state, material);

                                        /* Check for any stop condition */
                                        if (context->event &
                                            context_->step_event)
                                                break;
                                }

                                /* Update the locals if needed. */
                                if (step_max_locals > 0.) {
                                        context->medium(
                                            context, state, NULL, NULL);
                                        step_max_locals = transport_set_locals(
                                            context, medium, state, locals);
                                        if (locals->api.density <= 0.) {
                                                ERROR_REGISTER_NEGATIVE_DENSITY(
                                                    physics->material_name
                                                        [medium->material]);
                                                return context_->step_event;
                                        }
                                }
                        } else if (context_->step_event & PUMAS_EVENT_MEDIUM) {
                                /* A medium change occured. Let's update the
                                 * medium.
                                 */
                                medium = new_medium;
                                if ((medium == NULL) ||
                                    (context->event & PUMAS_EVENT_MEDIUM))
                                        break;
                                material = medium->material;
                                context->medium(context, state, NULL, NULL);
                                memset(&locals->api, 0x0, sizeof(locals->api));
                                locals->magnetized = 0;
                                step_max_locals = transport_set_locals(
                                    context, medium, state, locals);
                                if (locals->api.density <= 0.) {
                                        ERROR_REGISTER_NEGATIVE_DENSITY(
                                            physics->material_name
                                                [medium->material]);
                                        return context_->step_event;
                                }
                                straight = ((context->mode.scattering ==
                                    PUMAS_MODE_DISABLED) &&
                                    (scheme <= PUMAS_MODE_MIXED) &&
                                    (step_max_locals <= 0.) &&
                                    !locals->magnetized) ?
                                    1 :
                                    0;

                                /* Update the kinetic limit converted
                                 * to grammage for this material.
                                 */
                                enum pumas_mode tmp_scheme =
                                    scheme > PUMAS_MODE_DISABLED ?
                                    scheme :
                                    PUMAS_MODE_CSDA;
                                context_->step_X_limit =
                                    (context->event &
                                        PUMAS_EVENT_LIMIT_ENERGY) ?
                                    cel_grammage(physics, context, tmp_scheme,
                                        material, context->limit.energy) :
                                    0.;

                                /* Reset the stepping data memory. */
                                context_->step_first = 1;
                                context_->step_invlb1 = 0;

                                /* Record the change of medium. */
                                if ((record) &&
                                    !(context->event & PUMAS_EVENT_MEDIUM))
                                        record_state(context, medium,
                                            context_->step_event, state);
                        } else {
                                /*  This should not happen. */
                                assert(0);
                        }

                        /* Update the initial conditions and the tracking of
                         * stepping events.
                         */
                        ti = state->time;
                        wi = state->weight;
                        Xi = state->grammage;
                        if (context->mode.energy_loss >= PUMAS_MODE_CSDA) {
                                const double ki = state->energy;
                                Xf = cel_grammage(
                                    physics, context, scheme, material, ki);
                                dei = 1. / cel_energy_loss(physics, context,
                                               scheme, material, ki);
                        }
                        transport_limit(physics, context, state, material, Xi,
                            Xf, &grammage_max);
                        if (context_->step_event) break;
                }
        }

        /* Protect final kinetic energy value against rounding errors. */
        if (context->mode.direction == PUMAS_MODE_FORWARD) {
                const double kinetic_min =
                    (context->event & PUMAS_EVENT_LIMIT_ENERGY) ?
                    context->limit.energy :
                    0.;
                if (fabs(state->energy - kinetic_min) < FLT_EPSILON)
                        state->energy = kinetic_min;
        } else {
                if ((context->event & PUMAS_EVENT_LIMIT_ENERGY) &&
                    (fabs(state->energy - context->limit.energy) <
                        FLT_EPSILON))
                        state->energy = context->limit.energy;
        }

        /* Register the end of the track, if recording. */
        if (record)
                record_state(context, medium,
                    context_->step_event | PUMAS_EVENT_STOP, state);

        *medium_ptr = medium;
        return context_->step_event;
}

/**
 * Set the local properties of a medium.
 *
 * @param context The simulation context.
 * @param state   The Monte-Carlo state.
 * @param locals  The local properties.
 * @return The proposed max step length is returned. A null or negative value
 * indicates a uniform and infinite medium.
 *
 * This is an encapsulation of the API locals callback where internal data are
 * also initialised.
 */
double transport_set_locals(const struct pumas_context * context,
    struct pumas_medium * medium, struct pumas_state * state,
    struct medium_locals * locals)
{
        struct pumas_locals * loc = (struct pumas_locals *)locals;
        if (medium->locals == NULL) {
                loc->density =
                    locals->physics->material_density[medium->material];
                memset(loc->magnet, 0x0, sizeof(loc->magnet));
                locals->magnetized = 0;

                return 0;
        } else {
                const double step_max = medium->locals(medium, state, loc);
                if (loc->density <= 0) {
                        loc->density =
                            locals->physics->material_density[medium->material];
                }

                const double * const b = loc->magnet;
                locals->magnetized =
                    ((b[0] != 0.) || (b[1] != 0.) || (b[2] != 0.)) ? 1 : 0;
                return step_max * context->accuracy;
        }
}

/**
 * Prepare the various limits for a MC propagation.
 *
 * @param Physics      Handle for physics tables.
 * @param context      The simulation context.
 * @param material     The index of the propagation material.
 * @param ki           The kinetic energy.
 * @param Xi           The total travelled grammage.
 * @param Xf           The total grammage over which the particle can travel.
 * @param grammage_max The total grammage at which a limit is reached.
 *
 * Compute the limit, translated into grammage, for Monte-Carlo steps.
 * At return *distance* is overwriten with the new limit and the context event
 * flags are updated.
 */
void transport_limit(const struct pumas_physics * physics,
    struct pumas_context * context, const struct pumas_state * state,
    int material, double Xi, double Xf, double * grammage_max)
{
        /* Initialise the stepping event flags. */
        struct simulation_context * const context_ =
            (struct simulation_context *)context;
        context_->step_event = context_->step_foreseen = PUMAS_EVENT_NONE;

        /* Check the Monte-Carlo weight. */
        if (state->weight <= 0.) {
                context_->step_event = PUMAS_EVENT_WEIGHT;
                return;
        }

        /* Check the kinetic limits. */
        if (context->mode.direction == PUMAS_MODE_FORWARD) {
                const double kinetic_min =
                    (context->event & PUMAS_EVENT_LIMIT_ENERGY) ?
                    context->limit.energy :
                    0.;
                if (state->energy <= kinetic_min) {
                        context_->step_event = PUMAS_EVENT_LIMIT_ENERGY;
                        return;
                }
        } else if ((context->event & PUMAS_EVENT_LIMIT_ENERGY) &&
            (state->energy >= context->limit.energy)) {
                context_->step_event = PUMAS_EVENT_LIMIT_ENERGY;
                return;
        };

        /* Initialise with the context grammage limit. */
        if (context->event & PUMAS_EVENT_LIMIT_GRAMMAGE) {
                *grammage_max = context->limit.grammage;
                context_->step_foreseen = PUMAS_EVENT_LIMIT_GRAMMAGE;

        } else {
                *grammage_max = 0.;
        }

        /* Check the NO LOSS case. */
        const enum pumas_mode scheme = context->mode.energy_loss;
        if (scheme == PUMAS_MODE_DISABLED) {
                if (context->mode.scattering == PUMAS_MODE_MIXED) {
                        const double X = Xi -
                            coulomb_ehs_length(
                                physics, context, material, state->energy) *
                                log(context->random(context));
                        if ((*grammage_max == 0.) || (X < *grammage_max)) {
                                *grammage_max = X;
                                context_->step_foreseen =
                                    PUMAS_EVENT_VERTEX_COULOMB;
                                return;
                        }
                }
                return;
        }

        /* Check for an inelastic DEL. */
        const double sgn =
            (context->mode.direction == PUMAS_MODE_FORWARD) ? 1. : -1.;
        enum pumas_event foreseen = PUMAS_EVENT_NONE;
        double kinetic_limit = 0.;
        if (scheme == PUMAS_MODE_MIXED) {
#define MAX_TRIALS 1000 /* Sanity check against bugs e.g. in the PRNG */
                int i;
                for (i = 0; i < MAX_TRIALS; i++) {
                        const double zeta = context->random(context);
                        if ((zeta <= 0.) || (zeta >= 1.)) continue;

                        const double nI = del_interaction_length(
                            physics, context, material, state->energy) +
                            sgn * log(zeta);
                        if (nI <= 0.) break;

                        const double k = del_kinetic_from_interaction_length(
                            physics, context, material, nI);

                        /* Sanity check against rounding errors */
                        if (context->mode.direction == PUMAS_MODE_FORWARD) {
                                if (k >= state->energy) continue;
                        } else {
                                if (k <= state->energy) continue;
                        }

                        if ((context->mode.direction == PUMAS_MODE_BACKWARD) ||
                            (k > *table_get_Kt(physics, material))) {
                                kinetic_limit = k;
                                foreseen = PUMAS_EVENT_VERTEX_DEL;
                        }

                        break;
                }
#undef MAX_TRIALS
        }

        /* Check for an EHS event. */
        if ((scheme < PUMAS_MODE_STRAGGLED) &&
            (context->mode.scattering == PUMAS_MODE_MIXED)) {
                const double nI = ehs_interaction_length(physics, context,
                                      scheme, material, state->energy) +
                    sgn * log(context->random(context));
                if (nI > 0.) {
                        const double k = ehs_kinetic_from_interaction_length(
                            physics, context, scheme, material, nI);
                        if ((kinetic_limit <= 0.) ||
                            ((context->mode.direction == PUMAS_MODE_FORWARD) &&
                             (k > kinetic_limit)) ||
                            ((context->mode.direction == PUMAS_MODE_BACKWARD) &&
                             (k < kinetic_limit))) {
                                kinetic_limit = k;
                                foreseen = PUMAS_EVENT_VERTEX_COULOMB;
                        }
                }
        }

        /* Return if no discrete event might occur. */
        if (foreseen == PUMAS_EVENT_NONE) return;

        /* Convert the kinetic limit to a grammage one and update. */
        const double X = Xi +
            sgn * (Xf - cel_grammage(
                            physics, context, scheme, material, kinetic_limit));
        if ((*grammage_max <= 0) || (X < *grammage_max)) {
                *grammage_max = X;
                context_->step_foreseen = foreseen;
        }
}

/**
 * Apply an inelastic DEL during the Monte-Carlo propagation.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param state    The initial/final state.
 */
void transport_do_del(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material)
{
        /* Update the energy. */
        double ki, kf;
        int process = -1;
        polar_function_t * polar_func;
        const struct atomic_element * element = NULL;
        if (context->mode.direction == PUMAS_MODE_FORWARD) {
                ki = state->energy;
                polar_func = del_randomise_forward(
                    physics, context, state, material, &process, &element);
                kf = state->energy;
        } else {
                kf = state->energy;
                polar_func = del_randomise_reverse(
                    physics, context, state, material, &process, &element);
                ki = state->energy;
        }

        /* Update the event flag */
        struct simulation_context * context_ =
            (struct simulation_context *)context;
        if (process < 0) {
                if (state->weight > 0)
                        context_->step_event = PUMAS_EVENT_NONE;
                else
                        context_->step_event = PUMAS_EVENT_WEIGHT;
        } else {
                enum pumas_event event_for_[N_DEL_PROCESSES + 1] = {
                        PUMAS_EVENT_VERTEX_BREMSSTRAHLUNG,
                        PUMAS_EVENT_VERTEX_PAIR_CREATION,
                        PUMAS_EVENT_VERTEX_PHOTONUCLEAR,
                        PUMAS_EVENT_VERTEX_DELTA_RAY,
                        PUMAS_EVENT_VERTEX
                };
                context_->step_event = event_for_[process];
        }

        /* Update the direction. */
        if ((context->mode.scattering == PUMAS_MODE_MIXED) &&
            (polar_func != NULL)) {
                if (kf > ki) {
                        const double tmp = kf;
                        kf = ki;
                        ki = tmp;
                }

                const double mu = polar_func(physics, context, element, ki, kf);
                step_rotate_direction(context, state, mu);
        }
}

/*
 * Apply an Elastic Hard Scattering (EHS) event during the Monte-Carlo
 * propagation.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param state    The initial/final state.
 */
void transport_do_ehs(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material)
{
        /* Unpack data. */
        struct simulation_context * ctx = (struct simulation_context *)context;
        struct coulomb_workspace * workspace = ctx->workspace;
        const double kinetic = state->energy;

        /* Get the cut-off angle and the soft scattering 1st moment. */
        double mu0 = 0., invlb1 = 0.;
        table_get_msc(physics, context, material, kinetic, &mu0, &invlb1);

        /* Compute the scattering parameters and the total Coulomb
         * cross section.
         */
        int i;
        struct coulomb_data * data;
        double cs_tot = 0.;
        for (i = 0, data = workspace->data; i < physics->elements_in[material];
             i++, data++) {
                /* Compute the scattering parameters. */
                const struct material_component * const component =
                    &physics->composition[material][i];
                const struct atomic_element * const element =
                    physics->element[component->element];
                double kinetic0;
                coulomb_frame_parameters(element->Z, element->A, physics->mass,
                    kinetic, &kinetic0, data->fCM);
                data->fspin = coulomb_spin_factor(physics->mass, kinetic);
                coulomb_screening_parameters(element->Z, element->A,
                    physics->mass, kinetic, kinetic0, &data->n_parameters,
                    data->amplitude, data->screening);
                coulomb_pole_reduction(data->n_parameters, data->amplitude,
                    data->screening, data->a, data->b, data->c);
                data->normalisation = component->fraction /
                    coulomb_normalisation(element->Z, element->A, physics->mass,
                        kinetic, kinetic0);

                /* Compute the restricted Coulomb cross-section in the
                 * CM frame.
                 */
                data->cs_hard = data->normalisation * coulomb_restricted_cs(
                    mu0, data->fspin, data->n_parameters, data->screening,
                    data->a, data->b, data->c);
                cs_tot += data->cs_hard;
        }

        /* Randomise the hard scatterer element. */
        const double cc0 = context->random(context) * cs_tot;
        int ihard = physics->elements_in[material] - 1;
        double cs = 0.;
        for (i = 0, data = workspace->data; i < physics->elements_in[material];
             i++, data++) {
                cs += data->cs_hard;
                if (cs >= cc0) {
                        ihard = i;
                        break;
                }
        }

        /* Compute the hard angular parameter using a rejection sampling
         * method with a Wentzel cross-section as upper bound.
         */
        data = workspace->data + ihard;
        const int n = data->n_parameters - 1;
        double A = data->screening[0];
        for (i = 1; i < n; i++) {
                const double Ai = data->screening[i];
                if (Ai < A) A = Ai;
        }
        double mu1;
        for (;;) {
                /*
                 * Randomise with a Wentzel cross-section, i.e.
                 * 1 / (1 + mu)^2.
                 */
                const double zeta = context->random(context);
                const double tmp = 1. + A - zeta * (1 - mu0);
                if (tmp <= 0.) continue;
                mu1 = (A + mu0) * (A + 1.) / tmp - A;
                if (mu1 < mu0) mu1 = mu0;
                else if (mu1 > 1.) mu1 = 1;

                /*
                 * Ratio of the actual elastic DCS to the Wentzel one.
                 */
                int i;
                double ratio = 0.;
                for (i = 0; i < n; i++) {
                        ratio += data->amplitude[i] * (A + mu1) /
                            (data->screening[i] + mu1);
                }
                ratio *= coulomb_nuclear_form_factor(mu1, data->screening[n]);
                ratio *= ratio * (1. - data->fspin * mu1);

                if (context->random(context) <= ratio) break;
        }

        /* Transform from the CM frame to the Lab frame. */
        const double gamma = data->fCM[0];
        const double tau = data->fCM[1];
        double mu;
        if (mu1 > 1E-06) {
                /* Use the exact expression */
                const double a = gamma * (tau + 1. - 2. * mu1);
                const double ct_h = a / sqrt(4. * mu1 * (1. - mu1) + a * a);
                mu = 0.5 * (1. - ct_h);
        } else {
                /* Use the asymptotic expression. Note that the exact one
                 * is numericaly unstable for small angles.
                 */
                const double d = gamma * (1. + tau);
                mu = mu1 / (d * d);
        }

        /* Apply the rotation. */
        step_rotate_direction(context, state, mu);
}

static inline double envelope_norm(double a, double xmin, double xmax)
{
        if (a == 1) {
                return log(xmax / xmin);
        } else {
                return (pow(xmin, 1. - a) - pow(xmax, 1. - a)) / (a - 1.);
        }
}

/**
 * Randomise an inelastic DEL in forward MC.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param state    The initial/final state.
 * @param material The index of the propagation material.
 * @param process  The index of the randomised process.
 * @param target   The targeted atomic element.
 * @return The polar function for the randomisation of the corresponding TT or
 * `NULL` if none.
 *
 * Below 10 GeV the DEL is randomised from a power law bias PDF. Above,
 * a ziggurat algorithm is used.
 */

polar_function_t * del_randomise_forward(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material,
    int * process, const struct atomic_element ** target)
{
#define MAX_TRIALS 100

        /* Check for a *do nothing* process. */
        if (state->energy <= *table_get_Kt(physics, material)) return NULL;

        /* Randomise the target element and the DEL sub-process. */
        struct del_info info;
        del_randomise_target(physics, context, state, material, &info);
        dcs_function_t * const dcs_func = dcs_get(info.process);
        const struct atomic_element * element = physics->element[info.element];
        *target = element;
        *process = info.process;

        if (info.process == 3) {
                state->energy = dcs_ionisation_randomise(physics,
                    context, element, state->energy, physics->cutoff);
                return polar_get(info.process);
        }

        /* Get the kinematic range */
        double qmin, qmax;
        dcs_get_range(info.process, element->Z, physics->mass, state->energy,
            &qmin, &qmax);
        double xmin = qmin / state->energy;
        if (xmin < physics->cutoff) xmin = physics->cutoff;
        const double xmax = qmax / state->energy;
        if (xmin >= xmax) {
                return NULL;
        }

        /* Get the envelopes */
        double a0, a1, b0, b1, p0;
        if (state->energy >= *table_get_K(physics, physics->n_energies - 1)) {
                const float * envelope = table_get_dcs_envelope(physics,
                    info.process, element->index, physics->n_energies - 1);
                a0 = (double)envelope[0];
                b0 = (double)envelope[1];
                p0 = 1.;
                a1 = b1 = 0.;
        } else {
                const int row = table_index(
                    physics, context, table_get_K(physics, 0), state->energy);
                const float * envelope0 = table_get_dcs_envelope(physics,
                    info.process, element->index, row);
                const float * envelope1 = table_get_dcs_envelope(physics,
                    info.process, element->index, row + 1);

                a0 = (double)envelope0[0];
                b0 = (double)envelope0[1];
                a1 = (double)envelope1[0];
                b1 = (double)envelope1[1];

                const double k0 = physics->table_K[row];
                const double k1 = physics->table_K[row + 1];
                const double h1 = (state->energy - k0) / (k1 - k0);
                const double h0 = 1. - h1;
                b0 *= h0;
                b1 *= h1;

                p0 = envelope_norm(a0, xmin, xmax) * b0;
                const double p1 = envelope_norm(a1, xmin, xmax) * b1;
                p0 /= p0 + p1;
        }

        /* Randomise using rejection sampling */
        double x = 0.;
        int i;
        for (i = 0; i < MAX_TRIALS; i++) {
                double u = context->random(context);
                double a;
                if (u < p0) {
                        a = a0;
                        u = u / p0;
                } else {
                        a = a1;
                        u = (u - p0) / (1. - p0);
                }
                if (a == 1) {
                        x = xmin * exp(u * log(xmax / xmin));
                } else {
                        const double am = a - 1.;
                        const double pmin = pow(xmin, -am);
                        const double pmax = pow(xmax, -am);
                        x = pow(pmin - u * (pmin - pmax), -1. / am);
                }

                const double penv = b0 * pow(x, -a0) + b1 * pow(x, -a1);
                const double d = dcs_evaluate(physics, context, dcs_func,
                    element, state->energy, state->energy * x);

                if (context->random(context) * penv <= d) break;
        }

        if (i == MAX_TRIALS) {
                return NULL;
        } else {
                state->energy *= (1. - x);
                return polar_get(info.process);
        }

#undef MAX_TRIALS
}

/**
 * Randomise an inelastic DEL in backward MC.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param state    The initial/final state.
 * @param material The index of the propagation material.
 * @param process  The index of the randomised process.
 * @param target   The targeted atomic element.
 * @return The polar function for the randomisation of the corresponding TT or
 * `NULL` if none.
 *
 * A mixture PDF with a weight function is used for the randomisation
 * of the ancestor's state. First we check for a *do nothing* event, then the
 * DEL is processed by randomising the initial kinetic energy over a power law
 * distribution and then randomising the target element.
 */
polar_function_t * del_randomise_reverse(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material,
    int * process, const struct atomic_element ** target)
{
        /* Check for a pure CEL event. */
        const double lnq0 = -log(physics->cutoff);
        const double kt = *table_get_Kt(physics, material);
        const double kf = state->energy;
        const double pCEL =
            (kf >= kt) ? 0. : lnq0 / (lnq0 + log(kt / (kt - kf)));
        if (pCEL && (context->random(context) < pCEL)) {
                state->weight /= pCEL;
                return NULL;
        }
        const double xf =
            (kf >= kt * (1. - physics->cutoff)) ? physics->cutoff : 1. - kf / kt;

        /* Randomise the initial energy with a bias model. */
        double xmax;
        const double m1 = physics->mass - ELECTRON_MASS;
        if (state->energy < 0.5 * m1 * m1 / ELECTRON_MASS) {
                const double m2 = physics->mass + ELECTRON_MASS;
                xmax = 2. * ELECTRON_MASS *
                    (state->energy + 2. * physics->mass) / (m2 * m2);
                if (xmax < xf) return NULL;
                else if (xmax > 1.) xmax = 1.;
        } else {
                xmax = 1.;
        }
        const double alpha = BMC_ALPHA;

        double r, w_bias;
        del_randomise_power_law(context, alpha, xf, xmax, &r, &w_bias);
        w_bias /= r; /* Jacobian factor. */
        state->energy /= r;

        /* Randomise the target element and the DEL process. */
        struct del_info info;
        info.reverse.Q = state->energy - kf;
        del_randomise_target(physics, context, state, material, &info);
        if (info.reverse.weight <= 0) {
                state->weight = 0.;
                return NULL;
        }

        state->weight *= w_bias * info.reverse.weight /
            (del_cross_section(physics, context, material, kf) *
             (1. - pCEL));
        *process = info.process;

        if (context->mode.scattering == PUMAS_MODE_DISABLED) {
                *target = NULL;
                return NULL;
        } else {
                *target = physics->element[info.element];
                return polar_get(info.process);
        }
}

/**
 * Randomise the DEL over a generic power law PDF.
 *
 * @param context The simulation context.
 * @param alpha The power law exponent.
 * @param xmin The minimum fractional energy transfer.
 * @param xmax The maximum fractional energy transfer.
 * @param p_r The fractional final energy.
 * @param p_w The biasing weight factor.
 *
 * Randomise the initial energy with a power law bias model. Due to rounding
 * it can happen that the randomised energy is negative instead of very large.
 * If this occurs we try another step.
 */
void del_randomise_power_law(struct pumas_context * context, double alpha,
    double xmin, double xmax, double * p_r, double * p_w)
{
        double r = 0., w_bias;
        if (alpha == 1.) {
                const double lnq = log(xmax / xmin);
                for (;;) {
                        const double z = context->random(context);
                        r = xmin * exp(z * lnq);
                        if ((r < xmin) || (r >= xmax)) continue;
                        w_bias = lnq * r;
                        break;
                }
        } else {
                const double a1 = 1. - alpha;
                const double x0 = pow(xmin, a1);
                const double x1 = pow(xmax, a1);
                for (;;) {
                        const double z = context->random(context);
                        const double tmp = x0 + z * (x1 - x0);
                        r = pow(tmp, 1. / a1);
                        if ((r < xmin) || (r >= xmax)) continue;
                        w_bias = (x1 - x0) * r / (a1 * tmp);
                        break;
                }
        }

        *p_r = 1. - r;
        *p_w = w_bias;
}

/**
 * Randomise the target element and the sub-process for a DEL.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The index of the propagation material.
 * @param state    The initial/final state.
 * @param  info    Temporary data for the DEL.
 *
 * In forward mode the target is selected according to the total cross-section.
 * In backward mode, since both the initial and final kinetic energy are known,
 * the target is selected according to the DCS. At output the *info* structure
 * is filled with the target element data and the MC weight, for backward mode.
 */
void del_randomise_target(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material,
    struct del_info * info)
{
        /* Interpolate the kinetic table. */
        int i1, i2;
        double h, dk;
        i1 = table_index(
            physics, context, table_get_K(physics, 0), state->energy);
        if (i1 < 0) {
                i1 = i2 = 0;
                h = dk = 0.;
        } else if (i1 >= physics->n_energies - 1) {
                i1 = i2 = physics->n_energies - 1;
                h = dk = 0.;
        } else {
                i2 = i1 + 1;
                const double K1 = *table_get_K(physics, i1);
                dk = *table_get_K(physics, i2) - K1;
                h = (state->energy - K1) / dk;
        }

        /* Randomise the target element and the DEL process. */
        const struct material_component * component;
        int ic, ic0 = 0, ip;
        for (ic = 0; ic < material; ic++) ic0 += physics->elements_in[ic];
        if (context->mode.direction == PUMAS_MODE_FORWARD) {
                /* Randomise according to the total cross section. */
                double zeta = context->random(context);
                component = physics->composition[material];
                for (ic = ic0; ic < ic0 + physics->elements_in[material];
                     ic++, component++)
                        for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                                const double * f =
                                    table_get_CSf(physics, ip, ic, i1);
                                const double * df =
                                    table_get_CSf_dK(physics, ip, ic, i1);
                                const double csf = math_pchip_interpolate(
                                    h, f[0], f[1], df[0] * dk, df[1] * dk);
                                if (!(zeta > csf)) {
                                        const double * n =
                                            table_get_CSn(physics, ip,
                                                component->element, i1);
                                        const double * dn =
                                            table_get_CSn_dK(
                                                physics, ip,
                                                component->element, i1);
                                        const double csn =
                                            math_pchip_interpolate(
                                                h, n[0], n[1], dn[0] * dk,
                                                dn[1] * dk);
                                        info->reverse.weight = 1. / csn;
                                        goto target_found;
                                }
                        }
                assert(0); /* We should never reach this point ... */
        } else {
                /* Randomise according to the differential cross section. */
                double stot = 0.;
                for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                        component = physics->composition[material];
                        for (ic = ic0;
                             ic < ic0 + physics->elements_in[material];
                             ic++, component++) {
                                const struct atomic_element * element =
                                    physics->element[component->element];
                                const double d = dcs_evaluate(physics, context,
                                    dcs_get(ip), element, state->energy,
                                    info->reverse.Q);
                                stot += d * component->fraction;
                        }
                }

                if (stot == 0.) {
                        info->reverse.weight = 0.;
                        return;
                } else {
                        info->reverse.weight = stot;
                }

                if (context->mode.scattering == PUMAS_MODE_DISABLED) {
                        info->element = physics->n_elements;
                        info->process = N_DEL_PROCESSES;
                        return;
                }

                double zeta;
                for (;;) {
                        /* Prevent rounding errors. */
                        zeta = context->random(context) * stot;
                        if ((zeta > 0.) && (zeta < stot)) break;
                }
                double s = 0.;
                component = physics->composition[material];
                for (ic = ic0; ic < ic0 + physics->elements_in[material];
                     ic++, component++)
                        for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                                const int iel = component->element;
                                const struct atomic_element * element =
                                    physics->element[iel];
                                const double si =
                                    dcs_evaluate(physics, context, dcs_get(ip),
                                        element, state->energy,
                                        info->reverse.Q) *
                                    component->fraction;
                                s += si;
                                if (!(zeta > s)) {
                                        goto target_found;
                                }
                        }
                assert(0); /* We should never reach this point ... */
        }

target_found:
        info->element = component->element;
        info->process = ip;
}

/*
 * Low level routines: MC stepping.
 */
/**
 * Perform a Monte-Carlo transport step in a single medium.
 *
 * @param Physics            Handle for physics tables.
 * @param context            The simulation context.
 * @param state              The initial/final state.
 * @param straight           Flag for a straight step.
 * @param medium_index       The index of the propagation medium.
 * @param locals             Handle for the local properties of the medium.
 * @param grammage_max       The maximum grammage until a limit is reached.
 * @param step_max_medium    The stepping limitation from medium boundaries.
 * @param step_max_type      The type of geometry step (approximate or exact).
 * @param step_max_locals    The stepping limitation from a non uniform medium.
 * @param out_index          The index of the end step medium or a negative
 *                           value if a boundary condition was reached.
 * @return On success `PUMAS_RETURN_SUCCESS` is returned otherwise
 * `PUMAS_ERROR`.
 *
 * At return the state kinetic energy, position, direction and distance are
 * updated. In case of a stochastic CEL the proper time is also updated.
 */
enum pumas_return step_transport(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int straight,
    struct pumas_medium * medium, struct medium_locals * locals,
    double grammage_max, double step_max_medium,
    enum pumas_step step_max_type, double * step_max_locals,
    struct pumas_medium ** out_medium, struct error_context * error_)
{
        /* Unpack the data. */
        struct simulation_context * const context_ =
            (struct simulation_context *)context;
        const enum pumas_mode scheme = context->mode.energy_loss;
        double * const direction = state->direction;
        double * const position = state->position;
        const int material = medium->material;
        const double density = locals->api.density;
        const double density_i = 1. / density;
        const double momentum =
            sqrt(state->energy * (state->energy + 2. * physics->mass));

        /* Total grammage for the initial kinetic energy.  */
        const int tmp_scheme =
            (scheme == PUMAS_MODE_DISABLED) ? PUMAS_MODE_CSDA : scheme;
        const double Xtot = cel_grammage(
            physics, context, tmp_scheme, material, state->energy);

        /* Compute the local step length. */
        double step_loc, rLarmor0 = 0., uT0[3] = {0.};
        double invlb1 = 0.;
        if (!straight) {
                /* Compute the kinetic step length. */
                double r = context->accuracy;
                const double k_threshold = 1E+09;
                if ((scheme == PUMAS_MODE_STRAGGLED) &&
                    (state->energy > k_threshold)) {
                        /* In detailed mode, at very high energies shorter
                         * steps are needed.
                         */
                        double f = k_threshold / state->energy;
                        if (f < 0.1) f = 0.1;
                        r *= f;
                }
                step_loc = r * density_i * Xtot;

                if (context->mode.scattering == PUMAS_MODE_MIXED) {
                        /* Compute the soft scattering path length. */
                        if (context_->step_first != 0) {
                                double mu0;
                                table_get_msc(physics, context, material,
                                    state->energy, &mu0, &invlb1);
                                invlb1 *= density;
                        } else
                                invlb1 = context_->step_invlb1;
                        const double stepT = context->accuracy / invlb1;
                        if (stepT < step_loc) step_loc = stepT;
                }

                /* Compute the Larmor radius and the magnetic deflection
                 * direction.
                 */
                if (locals->magnetized == 0) {
                        rLarmor0 = 0.;
                } else {
                        const double * const B = locals->api.magnet;
                        uT0[0] =
                            direction[1] * B[2] - direction[2] * B[1];
                        uT0[1] =
                            direction[2] * B[0] - direction[0] * B[2];
                        uT0[2] =
                            direction[0] * B[1] - direction[1] * B[0];
                        const double BT = sqrt(uT0[0] * uT0[0] +
                            uT0[1] * uT0[1] + uT0[2] * uT0[2]);
                        if (BT == 0.) {
                                rLarmor0 = 0.;
                        } else {
                                rLarmor0 = momentum / (BT * LARMOR_FACTOR);
                                const double pinv = 1. / momentum;
                                uT0[0] *= pinv;
                                uT0[1] *= pinv;
                                uT0[2] *= pinv;
                        }
                }

                /* Update the step length. */
                if (rLarmor0 > 0.) {
                        const double stepM = context->accuracy * rLarmor0;
                        if (stepM < step_loc) step_loc = stepM;
                }
        } else {
                /* This is a straight step. */
                if (grammage_max <= 0.)
                        step_loc = 1E+09 * density_i;
                else
                        step_loc = grammage_max * density_i;
        }

        /* Update the `first step` flag. */
        context_->step_first = 0;

        /* Check the spatial resolution and the geometry step length. */
        if (step_loc < STEP_MIN) step_loc = STEP_MIN;
        double step = (step_max_medium <= 0) ?
            step_loc :
            (step_max_medium < step_loc ? step_max_medium : step_loc);
        if ((*step_max_locals > 0.) && (step > *step_max_locals))
                step = *step_max_locals;

        /* Check the total distance limitation. */
        *out_medium = medium;
        enum pumas_event event = PUMAS_EVENT_NONE;
        if (context->event & PUMAS_EVENT_LIMIT_DISTANCE) {
                const double d = context->limit.distance - state->distance;
                if (d <= step) {
                        step = d;
                        event = PUMAS_EVENT_LIMIT_DISTANCE;
                }
        }

        /* Update the position. */
        const double sgn =
            (context->mode.direction == PUMAS_MODE_FORWARD) ? 1. : -1.;
        position[0] += step * sgn * direction[0];
        position[1] += step * sgn * direction[1];
        position[2] += step * sgn * direction[2];

        /* Check for a change of medium. */
        struct pumas_medium * end_medium = NULL;
        context->medium(context, state, &end_medium, NULL);
        double end_position[3] = { position[0], position[1], position[2] };
        if (end_medium != medium) {
                if (step_max_type == PUMAS_STEP_CHECK) {
                        /* Check for an exact boundary. */
                        const double step_min =
                            (step < STEP_MIN) ? step : STEP_MIN;
                        double pi[3];
                        memcpy(pi, position, sizeof(pi));
                        double s1 = 0., s2 = -step_min;
                        position[0] = pi[0] + s2 * sgn * direction[0];
                        position[1] = pi[1] + s2 * sgn * direction[1];
                        position[2] = pi[2] + s2 * sgn * direction[2];
                        struct pumas_medium * tmp_medium = NULL;
                        context->medium(context, state, &tmp_medium, NULL);
                        if (tmp_medium != medium) {
                                /* Locate the medium change by dichotomy. */
                                if (tmp_medium != end_medium)
                                        end_medium = tmp_medium;
                                s1 = s2;
                                s2 = -step;
                                while (fabs(s1 - s2) > STEP_MIN) {
                                        double s3 = 0.5 * (s1 + s2);
                                        position[0] =
                                            pi[0] + s3 * sgn * direction[0];
                                        position[1] =
                                            pi[1] + s3 * sgn * direction[1];
                                        position[2] =
                                            pi[2] + s3 * sgn * direction[2];
                                        tmp_medium = NULL;
                                        context->medium(
                                            context, state, &tmp_medium, NULL);
                                        if (tmp_medium == medium) {
                                                s2 = s3;
                                        } else {
                                                s1 = s3;
                                                /* Update the end medium if
                                                 * required.
                                                 */
                                                if (tmp_medium != end_medium)
                                                        end_medium = tmp_medium;
                                        }
                                }
                                position[0] = pi[0] + s2 * sgn * direction[0];
                                position[1] = pi[1] + s2 * sgn * direction[1];
                                position[2] = pi[2] + s2 * sgn * direction[2];
                                step += s1;
                                end_position[0] =
                                    pi[0] + s1 * sgn * direction[0];
                                end_position[1] =
                                    pi[1] + s1 * sgn * direction[1];
                                end_position[2] =
                                    pi[2] + s1 * sgn * direction[2];

                                /* Force the last medium call to occur at the
                                 * final position. */
                                tmp_medium = NULL;
                                context->medium(
                                    context, state, &tmp_medium, NULL);
                        }
                }
                event = PUMAS_EVENT_MEDIUM;
                *out_medium = end_medium;
        }

        /*  Get the end step locals. */
        double Bi[3] = { 0., 0., 0. };
        if (locals->magnetized != 0) {
                Bi[0] = locals->api.magnet[0];
                Bi[1] = locals->api.magnet[1];
                Bi[2] = locals->api.magnet[2];
        }
        if ((*step_max_locals > 0.) && ((step_max_type != PUMAS_STEP_RAW) ||
            (event != PUMAS_EVENT_MEDIUM))) {
                /* Update the locals. */
                *step_max_locals = transport_set_locals(
                    context, medium, state, locals);
                if (locals->api.density <= 0.) {
                        ERROR_REGISTER_NEGATIVE_DENSITY(
                            physics->material_name[medium->material]);
                        return PUMAS_RETURN_DENSITY_ERROR;
                }
        }

        /* Offset the end step position for a boundary crossing. */
        if (event & PUMAS_EVENT_MEDIUM) {
                position[0] = end_position[0];
                position[1] = end_position[1];
                position[2] = end_position[2];
        }

        /* Set the end step kinetic energy. */
        double k1 = state->energy, dk = 0.;
        const double dX = 0.5 * step * (density + locals->api.density);
        if ((scheme >= PUMAS_MODE_CSDA) && (scheme <= PUMAS_MODE_MIXED)) {
                /* Deterministic CEL with check for any kinetic limit. */
                const double X = Xtot - sgn * dX;
                if ((context->mode.direction == PUMAS_MODE_FORWARD) &&
                    (X <= context_->step_X_limit)) {
                        k1 = (context->event & PUMAS_EVENT_LIMIT_ENERGY) ?
                            context->limit.energy :
                            0.;
                        const double grammage =
                            state->grammage + Xtot - context_->step_X_limit;
                        if ((grammage_max <= 0.) || (grammage < grammage_max)) {
                                grammage_max = grammage;
                                event = PUMAS_EVENT_LIMIT_ENERGY;
                        }
                } else if ((context->mode.direction == PUMAS_MODE_BACKWARD) &&
                    (context_->step_X_limit > 0.) &&
                    (X > context_->step_X_limit)) {
                        k1 = (context->event & PUMAS_EVENT_LIMIT_ENERGY) ?
                            context->limit.energy :
                            0.;
                        const double grammage =
                            state->grammage + context_->step_X_limit - Xtot;
                        if ((grammage_max <= 0.) || (grammage < grammage_max)) {
                                grammage_max = grammage;
                                event = PUMAS_EVENT_LIMIT_ENERGY;
                        }
                } else if (dX < EPSILON_X * Xtot) {
                        k1 -= sgn * cel_energy_loss(physics, context, scheme,
                            material, k1) * dX;
                        if ((context->event & PUMAS_EVENT_LIMIT_ENERGY) &&
                            (context->limit.energy > 0.)) {
                                if (((context->mode.direction ==
                                     PUMAS_MODE_FORWARD) &&
                                     (k1 < context->limit.energy)) ||
                                    ((context->mode.direction ==
                                    PUMAS_MODE_BACKWARD) &&
                                     (k1 > context->limit.energy))) {
                                        k1 = context->limit.energy;
                                        event = PUMAS_EVENT_LIMIT_ENERGY;
                                }
                        } else if (k1 < 0.) {
                                k1 = 0.;
                        }
                } else {
                        k1 = cel_kinetic_energy(
                            physics, context, scheme, material, X);
                }
        } else if (scheme == PUMAS_MODE_STRAGGLED) {
                /* Fluctuate the CEL around its average value. */
                double ratio;
                step_fluctuate(
                    physics, context, state, material, Xtot, dX, &k1, &ratio);
                dk = fabs(state->energy - k1);

                /* Check for a kinetic limit. */
                double kinetic_limit = -1.;
                if (context->mode.direction == PUMAS_MODE_FORWARD) {
                        const double kinetic_min =
                            (context->event & PUMAS_EVENT_LIMIT_ENERGY) ?
                            context->limit.energy :
                            0.;
                        if (k1 <= kinetic_min) kinetic_limit = kinetic_min;
                } else {
                        if ((context->event & PUMAS_EVENT_LIMIT_ENERGY) &&
                            (k1 >= context->limit.energy))
                                kinetic_limit = context->limit.energy;
                }
                if (kinetic_limit >= 0.) {
                        const double grammage = state->grammage +
                            fabs(state->energy - kinetic_limit) / dk * dX;
                        k1 = kinetic_limit;
                        if ((grammage_max <= 0.) || (grammage < grammage_max)) {
                                grammage_max = grammage;
                                event = PUMAS_EVENT_LIMIT_ENERGY;
                        }
                }

                /* Check for discrete events. */
                double Xmax = dX;
                if (grammage_max > 0.) {
                        const double dX1 = grammage_max - state->grammage;
                        if (Xmax > dX1) Xmax = dX1;
                }
                double k_x = -1.;

                /* Randomise an inelastic DEL. */
                double xs_del = del_cross_section(
                    physics, context, material, state->energy);
                const double tmp_del =
                    del_cross_section(physics, context, material, k1);
                if (tmp_del > xs_del) xs_del = tmp_del;
                if (xs_del <= 0.) goto no_del_event;
                const double X_del = -log(context->random(context)) / xs_del;
                if (X_del >= Xmax) goto no_del_event;
                const double k_h = cel_kinetic_energy(
                    physics, context, scheme, material, Xtot - sgn * X_del);
                const double k_del =
                    state->energy - sgn * ratio * fabs(state->energy - k_h);
                if (k_del <= 0.) goto no_del_event;
                const double r =
                    del_cross_section(physics, context, material, k_del) /
                    xs_del;
                if (context->random(context) > r) goto no_del_event;
                k_x = k_del;
                Xmax = X_del;
                event = PUMAS_EVENT_VERTEX_DEL;
        no_del_event:

                if (context->mode.scattering == PUMAS_MODE_MIXED) {
                        /* Randomise an EHS. */
                        double kmin, kmax;
                        if (k1 <= state->energy)
                                kmin = k1, kmax = state->energy;
                        else
                                kmax = k1, kmin = state->energy;
                        if (kmin < physics->table_K[1]) {
                                const double tmp = kmin;
                                kmin = kmax, kmax = tmp;
                        }
                        double lb_ehs = coulomb_ehs_length(
                            physics, context, material, kmin);
                        if (lb_ehs <= 0.) goto no_ehs_event;
                        const double X_ehs =
                            -log(context->random(context)) * lb_ehs;
                        if (X_ehs >= Xmax) goto no_ehs_event;
                        const double k_h = cel_kinetic_energy(
                            physics, context, scheme, material,
                            Xtot - sgn * X_ehs);
                        const double k_ehs = state->energy -
                            sgn * ratio * fabs(state->energy - k_h);
                        if (k_ehs <= 0.) goto no_ehs_event;
                        const double r = coulomb_ehs_length(physics, context,
                                             material, k_ehs) /
                            lb_ehs;
                        if ((r <= 0.) || (context->random(context) > 1. / r))
                                goto no_ehs_event;
                        k_x = k_ehs;
                        Xmax = X_ehs;
                        event = PUMAS_EVENT_VERTEX_COULOMB;
                no_ehs_event:;
                }

                /* Apply the discrete event, if any. */
                if (k_x >= 0.) {
                        k1 = k_x;
                        grammage_max = state->grammage + Xmax;
                }
        }

        /* Check and update the grammage. */
        const double Xi = state->grammage;
        state->grammage += dX;
        const double sf0 = step;
        double h_int = 0.;
        if ((grammage_max > 0.) && (state->grammage >= grammage_max)) {
                const double dX_ = grammage_max - Xi;
                if ((fabs(density - locals->api.density) <= FLT_EPSILON) ||
                    (sf0 <= FLT_EPSILON)) {
                        step = dX_ * density_i;
                        h_int = dX_ / (state->grammage - Xi);
                } else {
                        const double drho = locals->api.density - density;
                        double tmp =
                            density * density + 2. * dX_ * drho / sf0;
                        tmp = (tmp > FLT_EPSILON) ? sqrt(tmp) : 0;
                        h_int = (tmp - density) / drho;
                        step *= h_int;
                }
                state->grammage = grammage_max;
                if (!(event & PUMAS_EVENT_LIMIT_ENERGY)) {
                        /*  Update the kinetic energy. */
                        if (scheme <= PUMAS_MODE_MIXED) {
                                if (scheme != PUMAS_MODE_DISABLED)
                                        k1 = cel_kinetic_energy(physics,
                                            context, scheme, material, Xtot -
                                                sgn * (state->grammage - Xi));
                                event = context_->step_foreseen;
                        } else if (!(event & (PUMAS_EVENT_VERTEX_COULOMB |
                                                 PUMAS_EVENT_VERTEX_DEL))) {
                                /*
                                 * This is a grammage limit in the detailed
                                 * scheme.
                                 */
                                k1 = state->energy -
                                    sgn * (state->grammage - Xi) * dk / dX;
                                if (k1 < 0.) k1 = 0.;
                                event = PUMAS_EVENT_LIMIT_GRAMMAGE;
                        }
                }

                /* Update the position. */
                const double ds_ = step - sf0;
                position[0] += ds_ * sgn * direction[0];
                position[1] += ds_ * sgn * direction[1];
                position[2] += ds_ * sgn * direction[2];
        }

        /* Check the proper time limit. */
        int decayed = 0;
        double time_max = (context->event & PUMAS_EVENT_LIMIT_TIME) ?
            context->limit.time : 0.;
        if (context->mode.decay == PUMAS_MODE_RANDOMISED) {
                if ((time_max <= 0.) || (context_->lifetime < time_max)) {
                        time_max = context_->lifetime;
                        decayed = 1;
                }
        }

        const double sf1 = step;
        if (straight && (scheme != PUMAS_MODE_DISABLED)) {
                const double Ti = cel_proper_time(
                    physics, context, scheme, material, state->energy);
                if (time_max > 0.) {
                        const double Tf =
                            Ti - sgn * (time_max - state->time) * density;
                        if (Tf > 0.) {
                                const double dxT = fabs(
                                    Xtot - cel_grammage_as_time(physics,
                                               context, scheme, material, Tf));
                                if (Xi + dxT < state->grammage) {
                                        /* A proper time limit is reached. */
                                        event = PUMAS_EVENT_LIMIT_TIME;
                                        state->time = time_max;
                                        state->decayed = decayed;
                                        step = dxT * density_i;
                                        if (step > sf1) step = sf1;
                                        state->grammage = Xi + dxT;
                                        const double xf = Xtot - sgn * dxT;
                                        k1 = cel_kinetic_energy(physics,
                                            context, scheme, material, xf);
                                }
                        }
                }

                if (event != PUMAS_EVENT_LIMIT_TIME) {
                        const double Tf = cel_proper_time(
                            physics, context, scheme, material, k1);
                        state->time += fabs(Tf - Ti) * density_i;
                }
        } else {
                const double p_f =
                    (k1 <= 0) ? momentum : sqrt(k1 * (k1 + 2. * physics->mass));
                const double ti = state->time;
                state->time +=
                    0.5 * step * physics->mass * (1. / momentum + 1. / p_f);
                if ((time_max > 0.) && (state->time >= time_max)) {
                        /* A proper time limit is reached. */
                        event = PUMAS_EVENT_LIMIT_TIME;
                        state->time = time_max;
                        state->decayed = decayed;

                        /* Interpolate the step length. */
                        const double a = (momentum / p_f - 1.) / sf1;
                        const double c =
                            -2. * (time_max - ti) * momentum / physics->mass;
                        if (a != 0.) {
                                double delta = 1. - a * c;
                                if (delta < 0.) delta = 0.;
                                step = 1. / a * (sqrt(delta) - 1.);
                        } else {
                                step = -0.5 * c;
                        }
                        if (step < 0.) step = 0.;

                        /*  Update the kinetic energy. */
                        if (scheme != PUMAS_MODE_DISABLED) {
                                const double p1 = momentum / (1 + a * step);
                                k1 = sqrt(p1 * p1 -
                                         physics->mass * physics->mass) -
                                    physics->mass;
                                if (k1 < 0.) k1 = 0.;
                        }

                        /* Correct the grammage. */
                        const double Xf = Xi + dX;
                        if (density == locals->api.density)
                                state->grammage = Xi + step * density;
                        else
                                state->grammage = Xi +
                                    step * (density +
                                               0.5 * step / sf0 *
                                                   (locals->api.density -
                                                       density));
                        if (state->grammage > Xf) state->grammage = Xf;
                }
        }

        if (event == PUMAS_EVENT_LIMIT_TIME) {
                /* Correct the position. */
                const double ds_ = step - sf1;
                position[0] += ds_ * sgn * direction[0];
                position[1] += ds_ * sgn * direction[1];
                position[2] += ds_ * sgn * direction[2];
        }

        /* Update the event flag. */
        context_->step_event = event;

        /* Update the kinetic energy. */
        state->energy = k1;

        /* Update the travelled distance. */
        state->distance += step;

        /* Compute the multiple scattering path length. */
        if (context->mode.scattering == PUMAS_MODE_MIXED) {
                double mu0, invlb1_;
                if (state->energy <= 0.) {
                        context_->step_invlb1 = invlb1;
                } else {
                        table_get_msc(physics, context, material,
                            state->energy, &mu0, &invlb1_);
                        context_->step_invlb1 = locals->api.density * invlb1_;
                }
        }

        /* Update the end step properties. */
        double rLarmor1 = 0., uT1[3] = {0.};
        if (locals->magnetized) {
                /* Interpolate the end point magnetic field if needed. */
                double B[3] = { locals->api.magnet[0], locals->api.magnet[1],
                        locals->api.magnet[2] };
                int magnetized = locals->magnetized;
                if (h_int > 0.) {
                        B[0] = Bi[0] + h_int * (B[0] - Bi[0]);
                        B[1] = Bi[1] + h_int * (B[1] - Bi[1]);
                        B[2] = Bi[2] + h_int * (B[2] - Bi[2]);
                        magnetized =
                            (B[0] * B[0] + B[1] * B[1] + B[2] * B[2] == 0.);
                }

                /* Compute the Larmor radius and the magnetic deflection
                 * direction.
                 */
                if (magnetized == 0) {
                        rLarmor1 = 0.;
                        memset(uT1, 0x0, sizeof(uT1));
                } else {
                        uT1[0] = direction[1] * B[2] - direction[2] * B[1];
                        uT1[1] = direction[2] * B[0] - direction[0] * B[2];
                        uT1[2] = direction[0] * B[1] - direction[1] * B[0];
                        const double BT = sqrt(uT1[0] * uT1[0] +
                            uT1[1] * uT1[1] + uT1[2] * uT1[2]);
                        if (BT == 0.) {
                                rLarmor1 = 0.;
                        } else {
                                const double p = (state->energy <= 0) ?
                                    momentum :
                                    sqrt(state->energy *
                                        (state->energy + 2. * physics->mass));
                                rLarmor1 = p / (BT * LARMOR_FACTOR);
                                const double pinv = 1. / p;
                                uT1[0] *= pinv;
                                uT1[1] *= pinv;
                                uT1[2] *= pinv;
                        }
                }
        }

        /* Apply the magnetic deflection. */
        if ((rLarmor0 > 0.) || (rLarmor1 > 0.)) {
                const double u[3] = { direction[0], direction[1],
                        direction[2] };
                const double theta0 =
                    (rLarmor0 > 0.) ? state->charge * step / rLarmor0 : 0.;
                const double theta1 =
                    (rLarmor1 > 0.) ? state->charge * step / rLarmor1 : 0.;
                double theta = 0.5 * (theta0 + theta1);
                if (context->mode.direction == PUMAS_MODE_BACKWARD) {
                        theta = -theta;
                }
                double c = cos(theta);
                double s = sin(theta);
                double v[3];
                int i;
                for (i = 0; i < 3; i++)
                        v[i] = 0.5 * (uT0[i] + uT1[i]);
                double nrmv =
                    v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
                if (nrmv > 0.) {
                        nrmv = 1. / sqrt(nrmv);
                        for (i = 0; i < 3; i++) v[i] *= nrmv;
                } else {
                        c = 1.;
                        s = 0.;
                }
                for (i = 0; i < 3; i++)
                        direction[i] = c * u[i] + s * v[i];

                /* Safeguard against rounding errors */
                const double norm = 1. / sqrt(direction[0] * direction[0] +
                                             direction[1] * direction[1] +
                                             direction[2] * direction[2]);
                for (i = 0; i < 3; i++)
                        direction[i] *= norm;
        }

        /* Apply the multiple scattering. */
        if ((invlb1 > 0.) || (context_->step_invlb1 > 0.)) {
                double ilb1 = 0.25 * step * (invlb1 + context_->step_invlb1);
                if (ilb1 > 1.) ilb1 = 1.;
                double mu;
                do {
                        mu = -ilb1 * log(context->random(context));
                } while (mu > 1.);
                step_rotate_direction(context, state, mu);
        }

        return PUMAS_RETURN_SUCCESS;
}

/**
 * Apply a stochastic CEL on a MC step.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param state    The particle Monte-Carlo state.
 * @param material The target material.
 * @param Xtot     The initial CSDA total grammage.
 * @param dX       The step grammage length.
 * @param kf       The expected final state kinetic energy.
 * @param ratio    The ratio of the actual energy loss to the CSDA one.
 *
 * PENELOPE's model is used for the soft energy loss randomisation. Note that
 * `dE` differs from `ki-kf` if the kinetic energy falls to `0` before the
 * end of the step.
 */
static void step_fluctuate(const struct pumas_physics * physics,
    struct pumas_context * context, struct pumas_state * state, int material,
    double Xtot, double dX, double * kf, double * ratio)
{
        const enum pumas_mode scheme = context->mode.energy_loss;
        const double sgn =
            (context->mode.direction == PUMAS_MODE_FORWARD) ? 1. : -1.;
        double k1, r = 0.;
        k1 = cel_kinetic_energy(
            physics, context, scheme, material, Xtot - sgn * dX);
        const double dk0 = fabs(state->energy - k1);
        if ((k1 > 0.) && (dk0 > 0.)) {
                const double Omega0 = cel_straggling(
                    physics, context, material, state->energy);
                const double Omega1 = cel_straggling(
                    physics, context, material, k1);
                double dk12 = 0.5 * dX * (Omega0 + Omega1);
                const double tmp = dX / Xtot;
#define X_THRESHOLD 5E-02
                if (tmp > X_THRESHOLD) {
                        const double de1 = cel_energy_loss(
                            physics, context, scheme, material, k1);
                        const double de0 = cel_energy_loss(
                            physics, context, scheme, material, state->energy);
                        dk12 *= 1. + sgn * dX / dk0 * (de1 - de0);
                }
#undef X_THRESHOLD
                const double dk1 = sqrt(dk12);
                if (dk0 >= 3. * dk1) {
                        double u;
                        do
                                u = step_randn(context);
                        while (fabs(u) > 3.);
                        u /= 1.015387;
                        k1 += u * dk1;
                } else if (dk0 >= 1.7320508 * dk1) {
                        const double u =
                            1.7320508 * (1. - 2. * context->random(context));
                        k1 += u * dk1;
                } else {
                        const double dk32 = 3. * dk12;
                        const double dk02 = dk0 * dk0;
                        const double a =
                            1. - (dk32 - dk02) / (dk32 + 3. * dk02);
                        if (context->random(context) <= a) {
                                const double b = 0.5 * (dk32 + 3. * dk02) / dk0;
                                const double u = context->random(context);
                                k1 = state->energy - sgn * b * u;
                                if (k1 < 0.) k1 = 0.;
                        } else {
                                k1 = state->energy;
                        }
                }
                r = fabs(state->energy - k1) / dk0;
        }
        if (r == 0.) {
                r = 1.;
        }

        /* Copy back the result. */
        *kf = k1;
        *ratio = r;
}

/**
 * Gaussian normal random number.
 *
 * @param context The simulation context.
 * @return a random number distributed according to a normal distribution.
 *
 * The Box-Muller algorithm is used. The random variates are generated in pairs.
 * The *context* is used as local storage.
 */
static double step_randn(struct pumas_context * context)
{
        struct simulation_context * const context_ =
            (struct simulation_context *)context;
        context_->randn_done = !context_->randn_done;
        if (!context_->randn_done) return context_->randn_next;

        const double r = sqrt(-2. * log(context->random(context)));
        const double phi = 2. * M_PI * context->random(context);
        const double c = cos(phi);
        const double s = sin(phi);
        context_->randn_next = r * c;
        return r * s;
}

/**
 * Rotate the direction randomly but constraining the polar angle.
 *
 * @param context   The simulation context.
 * @param state     The initial/final state.
 * @param mu        The polar variable, as mu = 0.5 * (1 - cos(theta)).
 *
 * The direction is randomly rotated around the initial direction with the
 * constraint that the cosine of the angle between both directions is
 * *cos_theta*.
 */
void step_rotate_direction(struct pumas_context * context,
    struct pumas_state * state, double mu)
{
        /* Unpack data. */
        double * const direction = state->direction;

        /* Check the numerical sine. */
        const double cos_theta = 1 - 2 * mu;
        const double stsq = 4 * mu * (1. - mu);
        if (stsq <= 0.) return;
        const double st = sqrt(stsq);

        /* select the co-vectors for the local basis. */
        double u0x = 0., u0y = 0., u0z = 0.;
        const double a0 = fabs(direction[0]);
        const double a1 = fabs(direction[1]);
        const double a2 = fabs(direction[2]);
        if (a0 > a1) {
                if (a0 > a2) {
                        const double nrm =
                            1. / sqrt(direction[0] * direction[0] +
                                     direction[2] * direction[2]);
                        u0x = -direction[2] * nrm, u0z = direction[0] * nrm;
                } else {
                        const double nrm =
                            1. / sqrt(direction[1] * direction[1] +
                                     direction[2] * direction[2]);
                        u0y = direction[2] * nrm, u0z = -direction[1] * nrm;
                }
        } else {
                if (a1 > a2) {
                        const double nrm =
                            1. / sqrt(direction[0] * direction[0] +
                                     direction[1] * direction[1]);
                        u0x = direction[1] * nrm, u0y = -direction[0] * nrm;
                } else {
                        const double nrm =
                            1. / sqrt(direction[1] * direction[1] +
                                     direction[2] * direction[2]);
                        u0y = direction[2] * nrm, u0z = -direction[1] * nrm;
                }
        }
        const double u1x = u0y * direction[2] - u0z * direction[1];
        const double u1y = u0z * direction[0] - u0x * direction[2];
        const double u1z = u0x * direction[1] - u0y * direction[0];

        /* Apply the rotation. */
        const double phi = M_PI * (1. - 2. * context->random(context));
        const double cp = cos(phi);
        const double sp = sin(phi);
        direction[0] = cos_theta * direction[0] + st * (cp * u0x + sp * u1x);
        direction[1] = cos_theta * direction[1] + st * (cp * u0y + sp * u1y);
        direction[2] = cos_theta * direction[2] + st * (cp * u0z + sp * u1z);
}

/*
 * Low level routine: indexing of atomic elements.
 */
/**
 * Find the table index of an element given its name.
 *
 * @param Physics  Handle for physics tables.
 * @param name     The element name.
 * @return The element index or `-1` if it wasn't found.
 */
int element_index(const struct pumas_physics * physics, const char * name)
{
        int index = 0;
        for (; index < physics->n_elements; index++)
                if (strcmp(physics->element[index]->name, name) == 0)
                        return index;
        return -1;
}

/*
 * Low level routine: indexing of materials.
 */
/**
 * Find the table index of a material given its name.
 *
 * @param Physics  Handle for physics tables.
 * @param name     The material name.
 * @param error_   The error data.
 * @return On success `PUMAS_RETURN_SUCCESS` is returned otherwise an error
 * code is returned.
 */
static enum pumas_return material_index(const struct pumas_physics * physics,
    const char * material, int * index, struct error_context * error_)
{
        int i;
        for (i = 0; i < physics->n_materials; i++) {
                if (strcmp(physics->material_name[i], material) == 0) {
                        *index = i;
                        return PUMAS_RETURN_SUCCESS;
                }
        }

        return ERROR_VREGISTER(
            PUMAS_RETURN_UNKNOWN_MATERIAL, "unknown material `%s'", material);
}

double data_nuclear_radius(double Z, double A)
{
        /* Nuclear r.m.s. radii, in fm. */
        static const double rN[120] = {
            2.098, 0.858, 1.680, 2.400, 2.518, 2.405, 2.470, 2.548, 2.734,
            2.900, 2.993, 2.940, 3.043, 3.035, 3.098, 3.187, 3.245, 3.360,
            3.413, 3.408, 3.477, 3.443, 3.595, 3.600, 3.644, 3.681, 3.748,
            3.843, 3.776, 3.943, 3.942, 4.032, 4.065, 4.078, 4.123, 4.135,
            4.188, 4.209, 4.237, 4.249, 4.306, 4.318, 4.363, 4.388, 4.432,
            4.435, 4.479, 4.520, 4.612, 4.646, 4.640, 4.630, 4.714, 4.706,
            4.756, 4.774, 4.823, 4.848, 4.878, 4.897, 4.927, 4.948, 5.054,
            5.093, 5.133, 5.177, 5.197, 5.210, 5.264, 5.301, 5.390, 5.371,
            5.429, 5.479, 5.423, 5.400, 5.409, 5.411, 5.387, 5.318, 5.404,
            5.471, 5.498, 5.520, 5.520, 5.529, 5.629, 5.637, 5.661, 5.669,
            5.710, 5.701, 5.784, 5.825, 5.824, 5.817, 5.843, 5.843, 5.868,
            5.875, 5.906, 5.912, 5.918, 5.937, 5.967, 5.973, 5.979, 5.985,
            5.980, 6.033, 6.051, 6.056, 6.074, 6.079, 6.097, 6.097, 6.119,
            6.125, 6.125, 2.958};

        int iN = (int)Z;
        const int nN = sizeof(rN) / sizeof(rN[0]);
        if (iN >= nN - 1) iN = nN - 2;
        else if ((iN == 1) && (A >= 1.5)) {
                /* For hydrogen isotopes we use a different radius because
                 * it differs significantly.
                 */
                iN = 0;
        } else if ((iN == 11) && (A == 22.)) {
                /* For standard rock a fictious Rockium atom is used. */
                iN = nN - 1;
        }
        return rN[iN] * 1E-15; /* fm */
}

/*
 * Low level routines: various functions related to Coulomb scattering.
 */
/**
 * Compute the atomic and nuclear screening parameters.
 *
 * @param Z            The atomic charge number.
 * @param A            The atomic mass number.
 * @param mass         The projectile mass.
 * @param kinetic      The projectile kinetic energy.
 * @param kinetic0     The projectile kinetic energy in the CM frame.
 * @param n_parameters The number of screening parameters.
 * @param amplitude    The amplitudes of the atomic screening terms.
 * @param screening    The computed screening parameters.
 *
 * The atomic screening is parameterized by a sum of three exponentials
 * following Salvat et al. (1987). The nuclear form factor is set according to
 * the r.m.s. of the proton charge distribution taken from the compilation of De
 * Vries et al. (1987). In both cases, a Coulomb correction is applied according
 * to Kuraev et al. (2014).
 *
 * References:
 *      Salvat et al., Phys. Rev. A 36, 2 (1987)
 *      De Vries et al., Atom. Data & Nucl. Tables 36, 495 (1987)
 *      Kuraev et al., Phys. ReV. D 89, 116016 (2014)
 */
void coulomb_screening_parameters(double Z, double A, double mass,
    double kinetic, double kinetic0, int * n_parameters,
    double * amplitude, double * screening)
{
        /* KTV correction to the 1st Born approximation from Kuraev et al.
         * (2014).
         */
        const double aZE = ALPHA_EM * Z * (kinetic + mass);
        const double p2 = kinetic * (kinetic + 2. * mass);
        const double zeta2 = aZE * aZE / p2;
        double f;
        if (zeta2 < 1) {
                /* Use KTV serie expansion in zeta Riemann */
                f = 1. / (1. + zeta2) +
                    0.20205690315959424 -
                    0.03692775514336999 * zeta2 +
                    0.008349277381922926 * zeta2 * zeta2;
                f *= zeta2;
        } else {
                /* Use the asymptotic expansion of the digamma function */
                const double r2i = 1. / (1. + zeta2);
                const double phi = -atan(sqrt(zeta2));

                f = -0.5 * log(r2i) +
                    0.5 * (1. - r2i) +
                    1. / 12. * (1. - cos(2 * phi) * r2i) -
                    1. / 120. * (1. - cos(4 * phi) * r2i * r2i) +
                    1. / 252. * (1. - cos(6 * phi) * r2i * r2i * r2i);
        }
        const double ktv = exp(2 * f);

        /* Atomic screening parameters from Salvat et al. (1987) */
        static const double prefactor[2][103] = {{
             1.00000E+00,-2.25920E-01,6.04537E-01,3.27766E-01,
             2.32684E-01,1.53676E-01,9.95750E-02,6.25130E-02,3.68040E-02,
             1.88410E-02,7.44440E-01,6.42349E-01,6.00152E-01,5.15971E-01,
             4.38675E-01,5.45871E-01,7.24889E-01,2.19124E+00,4.85607E-02,
             5.80017E-01,5.54340E-01,1.11950E-02,3.18350E-02,1.07503E-01,
             4.97556E-02,5.11841E-02,5.00039E-02,4.73509E-02,7.70967E-02,
             4.00041E-02,1.08344E-01,6.09767E-02,2.11561E-02,4.83575E-01,
             4.50364E-01,4.19036E-01,1.73438E-01,3.35694E-02,6.88939E-02,
             1.17552E-01,2.55689E-01,2.69313E-01,2.20138E-01,2.75057E-01,
             2.71053E-01,2.78363E-01,2.56210E-01,2.27100E-01,2.49215E-01,
             2.15313E-01,1.80560E-01,1.30772E-01,5.88293E-02,4.45145E-01,
             2.70796E-01,1.72814E-01,1.94726E-01,1.91338E-01,1.86776E-01,
             1.66461E-01,1.62350E-01,1.58016E-01,1.53759E-01,1.58729E-01,
             1.45327E-01,1.41260E-01,1.37360E-01,1.33614E-01,1.29853E-01,
             1.26659E-01,1.28806E-01,1.30256E-01,1.38420E-01,1.50030E-01,
             1.60803E-01,1.72164E-01,1.83411E-01,2.23043E-01,2.28909E-01,
             2.09753E-01,2.70821E-01,2.37958E-01,2.28771E-01,1.94059E-01,
             1.49995E-01,9.55262E-02,3.19155E-01,2.40406E-01,2.26579E-01,
             2.17619E-01,2.41294E-01,2.44758E-01,2.46231E-01,2.55572E-01,
             2.53567E-01,2.43832E-01,2.41898E-01,2.44050E-01,2.40237E-01,
             2.34997E-01,2.32114E-01,2.27937E-01,2.29571E-01
            }, {
             0.00000E+00,1.22592E+00,3.95463E-01,6.72234E-01,
             7.67316E-01,8.46324E-01,9.00425E-01,9.37487E-01,9.63196E-01,
             9.81159E-01,2.55560E-01,3.57651E-01,3.99848E-01,4.84029E-01,
             5.61325E-01,-5.33329E-01,-7.54809E-01,-2.2852E+00,7.75935E-01,
             4.19983E-01,4.45660E-01,6.83176E-01,6.75303E-01,7.16172E-01,
             6.86632E-01,6.99533E-01,7.14201E-01,7.29404E-01,7.95083E-01,
             7.59034E-01,7.48941E-01,7.15671E-01,6.70932E-01,5.16425E-01,
             5.49636E-01,5.80964E-01,7.25336E-01,7.81581E-01,7.20203E-01,
             6.58088E-01,5.82051E-01,5.75262E-01,5.61797E-01,5.94338E-01,
             6.11921E-01,6.06653E-01,6.50520E-01,6.15496E-01,6.43990E-01,
             6.11497E-01,5.76688E-01,5.50366E-01,5.48174E-01,5.54855E-01,
             6.52415E-01,6.84485E-01,6.38429E-01,6.46684E-01,6.55810E-01,
             7.05677E-01,7.13311E-01,7.20978E-01,7.28385E-01,7.02414E-01,
             7.42619E-01,7.49352E-01,7.55797E-01,7.61947E-01,7.68005E-01,
             7.73365E-01,7.52781E-01,7.32428E-01,7.09596E-01,6.87141E-01,
             6.65932E-01,6.46849E-01,6.30598E-01,6.17575E-01,6.11402E-01,
             6.00426E-01,6.42829E-01,6.30789E-01,6.21959E-01,6.10455E-01,
             6.03147E-01,6.05994E-01,6.23324E-01,6.56665E-01,6.42246E-01,
             6.24013E-01,6.30394E-01,6.29816E-01,6.31596E-01,6.49005E-01,
             6.53604E-01,6.43738E-01,6.48850E-01,6.70318E-01,6.76319E-01,
             6.65571E-01,6.88406E-01,6.94394E-01,6.82014E-01
        }};

        static const double exponent[3][103] = {{
            1.11728E+00,5.52725E+00,2.81741E+00,4.54302E+00,
            5.99006E+00,8.04043E+00,1.08122E+01,1.48233E+01,2.14001E+01,
            3.49994E+01,4.12050E+00,4.72663E+00,5.14051E+00,5.84918E+00,
            6.67070E+00,6.37029E+00,6.21183E+00,5.54701E+00,3.02597E+01,
            6.32184E+00,6.63280E+00,9.97569E+01,4.25330E+01,1.89587E+01,
            3.18642E+01,3.18251E+01,3.29153E+01,3.47580E+01,2.53264E+01,
            4.03429E+01,2.01922E+01,2.91996E+01,6.24873E+01,8.78242E+00,
            9.33480E+00,9.91420E+00,1.71659E+01,5.52077E+01,3.13659E+01,
            2.20537E+01,1.42403E+01,1.40442E+01,1.59176E+01,1.43137E+01,
            1.46537E+01,1.46455E+01,1.55878E+01,1.69141E+01,1.61552E+01,
            1.77931E+01,1.98751E+01,2.41540E+01,3.99955E+01,1.18053E+01,
            1.65915E+01,2.23966E+01,2.07637E+01,2.12350E+01,2.18033E+01,
            2.39492E+01,2.45984E+01,2.52966E+01,2.60169E+01,2.54973E+01,
            2.75466E+01,2.83460E+01,2.91604E+01,2.99904E+01,3.08345E+01,
            3.16806E+01,3.13526E+01,3.12166E+01,3.00767E+01,2.86302E+01,
            2.75684E+01,2.65861E+01,2.57339E+01,2.29939E+01,2.28644E+01,
            2.44080E+01,2.09409E+01,2.29872E+01,2.37917E+01,2.66951E+01,
            3.18397E+01,4.34890E+01,2.00150E+01,2.45012E+01,2.56843E+01,
            2.65542E+01,2.51930E+01,2.52522E+01,2.54271E+01,2.51526E+01,
            2.55959E+01,2.65567E+01,2.70360E+01,2.72673E+01,2.79152E+01,
            2.86446E+01,2.93353E+01,3.01040E+01,3.02650E+01
        }, {
            1.00000E+00,2.39924E+00,6.62463E-01,9.85154E-01,
            1.21347E+00,1.49129E+00,1.76868E+00,2.04035E+00,2.30601E+00,
            2.56621E+00,8.71798E-01,1.00247E+00,1.01529E+00,1.17314E+00,
            1.34102E+00,2.55169E+00,3.38827E+00,4.56873E+00,3.12426E+00,
            1.00935E+00,1.10227E+00,4.12865E+00,3.94043E+00,3.06375E+00,
            3.78110E+00,3.77161E+00,3.79085E+00,3.82989E+00,3.39276E+00,
            3.94645E+00,3.47325E+00,4.12525E+00,4.95015E+00,1.69671E+00,
            1.79002E+00,1.88354E+00,3.11025E+00,4.28418E+00,4.24121E+00,
            4.03254E+00,2.97020E+00,2.86107E+00,3.36719E+00,2.73701E+00,
            2.71828E+00,2.61549E+00,2.74124E+00,3.08408E+00,2.88189E+00,
            3.29372E+00,3.80921E+00,4.61191E+00,5.91318E+00,1.79673E+00,
            2.69645E+00,3.45951E+00,3.46574E+00,3.48193E+00,3.50982E+00,
            3.51987E+00,3.55603E+00,3.59628E+00,3.63834E+00,3.73639E+00,
            3.72882E+00,3.77625E+00,3.82444E+00,3.87344E+00,3.92327E+00,
            3.97271E+00,4.09040E+00,4.20492E+00,4.24918E+00,4.24261E+00,
            4.23412E+00,4.19992E+00,4.14615E+00,3.73461E+00,3.69138E+00,
            3.96429E+00,3.24563E+00,3.62172E+00,3.77959E+00,4.25824E+00,
            4.92848E+00,5.85205E+00,2.90906E+00,3.55241E+00,3.79223E+00,
            4.00437E+00,3.67795E+00,3.63966E+00,3.61328E+00,3.43021E+00,
            3.43474E+00,3.59089E+00,3.59411E+00,3.48061E+00,3.50331E+00,
            3.61870E+00,3.55697E+00,3.58685E+00,3.64085E+00
        }, {
            1.00000E+00,1.00000E+00,1.00000E+00,1.00000E+00,
            1.00000E+00,1.00000E+00,1.00000E+00,1.00000E+00,1.00000E+00,
            1.00000E+00,1.00000E+00,1.00000E+00,1.00000E+00,1.00000E+00,
            1.00000E+00,1.67534E+00,1.85964E+00,2.04455E+00,7.32637E-01,
            1.00000E+00,1.00000E+00,1.00896E+00,1.05333E+00,1.00137E+00,
            1.12787E+00,1.16064E+00,1.19152E+00,1.22089E+00,1.14261E+00,
            1.27594E+00,1.00643E+00,1.18447E+00,1.35819E+00,1.00000E+00,
            1.00000E+00,1.00000E+00,7.17673E-01,8.57842E-01,9.47152E-01,
            1.01806E+00,1.01699E+00,1.05906E+00,1.15477E+00,1.10923E+00,
            1.12336E+00,1.43183E+00,1.14079E+00,1.26189E+00,9.94156E-01,
            1.14781E+00,1.28288E+00,1.41954E+00,1.54707E+00,1.00000E+00,
            6.81361E-01,8.07311E-01,8.91057E-01,9.01112E-01,9.10636E-01,
            8.48620E-01,8.56929E-01,8.65025E-01,8.73083E-01,9.54998E-01,
            8.88981E-01,8.96917E-01,9.04803E-01,9.12768E-01,9.20306E-01,
            9.28838E-01,1.00717E+00,1.09456E+00,1.16966E+00,1.23403E+00,
            1.29699E+00,1.35350E+00,1.40374E+00,1.44284E+00,1.48856E+00,
            1.53432E+00,1.11214E+00,1.23735E+00,1.25338E+00,1.35772E+00,
            1.46828E+00,1.57359E+00,7.20714E-01,8.37599E-01,9.33468E-01,
            1.02385E+00,9.69895E-01,9.82474E-01,9.92527E-01,9.32751E-01,
            9.41671E-01,1.01827E+00,1.02554E+00,9.66447E-01,9.74347E-01,
            1.04137E+00,9.90568E-01,9.98878E-01,1.04473E+00
        }};

        const double p02 = kinetic0 * (kinetic0 + 2. * mass);
        const double d = 0.25 * HBAR_C * HBAR_C / p02 * ktv;
        int iZ = (int)Z - 1;
        const int nZ = sizeof(prefactor[0]) / sizeof(prefactor[0][0]);
        if (iZ >= nZ) iZ = nZ - 1;
        int n;
        if (exponent[2][iZ] == 1.) {
                if (iZ == 0) {
                        /* Patch for hydrogen */
                        n = 1;
                        amplitude[0] = 1.;
                } else {
                        n = 2;
                        amplitude[0] = prefactor[0][iZ];
                        amplitude[1] = 1. - amplitude[0];
                }
        } else {
                n = 3;
                amplitude[0] = prefactor[0][iZ];
                amplitude[1] = prefactor[1][iZ];
                amplitude[2] = 1. - (amplitude[0] + amplitude[1]);
        }

        int i;
        for (i = 0; i < n; i++) {
                const double a = exponent[i][iZ] / BOHR_RADIUS;
                screening[i] = d * a * a;
        }

        /* Nuclear screening. */
        const double RN = data_nuclear_radius(Z, A);
        screening[n++] = 12. * d / (RN * RN); /* the factor 12 corresponds to
                                               * a 4th order pole used in
                                               * integrals as an approximation
                                               * to the nuclear form factor
                                               * (see below).
                                               */
        *n_parameters = n;
}

/**
 * Compute the elastic nuclear form factor.
 *
 * @param mu    The reduced angular parameter.
 * @param N     The nuclear screening factor.
 * @return The form factor.
 */
double coulomb_nuclear_form_factor(double mu, double N)
{
        if (mu <= 0.) return 1.;
        else if (mu > 1.) return 0.;

        const double x2 = 10. * mu / N; /* The factor 10 is there in order to
                                         * recover sqrt(5 / 6) * RN as
                                         * effective radius (see above).
                                         */
        double d;
        if (x2 <= 1E-04) {
                /* Use a taylor expansion. */
                d = 1. / (1. + 0.1 * x2);
        } else {
                /* Use the full expression. */
                const double x = sqrt(x2);
                d = 3 * (sin(x) - x * cos(x)) / (x2 * x);
        }

        return d * d;
}

/**
 * Compute the Coulomb macroscopic cross-section normalisation.
 *
 * @param Z         The atomic number of the target element.
 * @param A         The atomic mass of the target element.
 * @param mass      The projectile mass.
 * @param kinetic   The projectile kinetic energy.
 * @param kinetic0  The projectile kinetic energy in the CM frame.
 * @return The normalisation in kg/m^2.
 */
double coulomb_normalisation(
    double Z, double A, double mass, double kinetic, double kinetic0)
{
        const double p2 = kinetic * (kinetic + 2. * mass);
        const double p02 = kinetic0 * (kinetic0 + 2. * mass);
        double d = ALPHA_EM * Z * HBAR_C * (kinetic + mass);
        return A * 1E-03 * p2 * p02 / (d * d * M_PI * AVOGADRO_NUMBER);
}

/**
 * Encapsulation of the interaction length for EHS.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param material The propagation material.
 * @param kinetic  The kinetic energy.
 * @return The grammage length for hard scattering events.
 */
double coulomb_ehs_length(const struct pumas_physics * physics,
    struct pumas_context * context, int material, double kinetic)
{
        const double p2 = kinetic * (kinetic + 2. * physics->mass);
        const int imax = physics->n_energies - 1;
        if (kinetic < *table_get_K(physics, 1)) {
                return *table_get_Lb(physics, material, 1) / p2;
        } else if (kinetic >= *table_get_K(physics, imax)) {
                return *table_get_Lb(physics, material, imax) / p2;
        } else {
                const int i1 = table_index(
                    physics, context, table_get_K(physics, 0), kinetic);
                const int i2 = i1 + 1;
                double h = (kinetic - *table_get_K(physics, i1)) /
                    (*table_get_K(physics, i2) - *table_get_K(physics, i1));
                return (*table_get_Lb(physics, material, i1) +
                           h * (*table_get_Lb(physics, material, i2) -
                                   *table_get_Lb(physics, material, i1))) /
                    p2;
        }
}

/**
 * Compute the spin factor to the Coulomb DCS.
 *
 * @param mass    The projectile mass.
 * @param kinetic The projectile kinetic energy.
 * @return The spin factor.
 */
double coulomb_spin_factor(double mass, double kinetic)
{
        const double e = kinetic + mass;
        return kinetic * (e + mass) / (e * e);
}

/**
 * Compute the parameters of the CM to the Lab frame transform for the
 * Coulomb DCS.
 *
 * @param Z          The atomic charge number.
 * @param A          The atomic mass number.
 * @param mass       The projectile mass.
 * @param kinetic    The projectile kinetic energy.
 * @param kinetic0   The CM kinetic energy.
 * @param parameters The vector of Lorentz parameters (gamma, tau).
 */
void coulomb_frame_parameters(double Z, double A, double mass, double kinetic,
    double * kinetic0, double * parameters)
{
        const double Ma = Z * PROTON_MASS + (A - Z) * NEUTRON_MASS; 
        double M2 = mass + Ma;
        M2 *= M2;
        const double sCM12i = 1. / sqrt(M2 + 2. * Ma * kinetic);
        parameters[0] = (kinetic + mass + Ma) * sCM12i;
        *kinetic0 = (kinetic * Ma + mass * (mass + Ma)) * sCM12i - mass;
        if (*kinetic0 < 1E-09) *kinetic0 = 1E-09;
        const double etot = kinetic + mass + Ma;
        const double betaCM2 =
            kinetic * (kinetic + 2. * mass) / (etot * etot);
        double rM2 = mass / Ma;
        rM2 *= rM2;
        parameters[1] = sqrt(rM2 * (1. - betaCM2) + betaCM2);
}

/**
 * Compute the coefficients of the Coulomb DCS pole reduction.
 *
 * @param n_parameters The number of screening parameters.
 * @param amplitude The amplitudes of the atomic screening terms.
 * @param screening The screening parameters: Ai and N.
 * @param a         The vector of coefficients for 1st order poles.
 * @param b         The vector of coefficients for 2nd order poles.
 * @param b         The vector of coefficients for nuclear poles.
 *
 * The Coulomb DCS goes as sum(1/(Ai+mu))^2*N^2/(N+mu)^4, with
 * Ai the atomic screening angles and Ni the nuclear ones. It is reduced to a
 * sum over poles as: ai/(Ai+mu)+bi/(Ai+mu)^2+cj/(N+mu)^j.
 */
void coulomb_pole_reduction(int n_parameters, const double * amplitude,
    const double * screening, long double * a, long double * b, long double * c)
{
        const int n = n_parameters - 1;
        memset(a, 0x0, n * sizeof(a[0]));
        memset(b, 0x0, n * sizeof(b[0]));
        memset(c, 0x0, 4 * sizeof(c[0]));

        /* Compute the pole reduction without nuclear term */
        int i, j;
        for (i = 0; i < n; i++) {
                const long double Ai = amplitude[i];
                const long double Bi = screening[i];
                b[i] = Ai * Ai;
                for (j = i + 1; j < n; j++) {
                        const long double Aj = amplitude[j];
                        const long double Bj = screening[j];
                        const long double d = 2 * Ai * Aj / (Bj - Bi);
                        a[i] += d;
                        a[j] -= d;
                }
        }

        /* Compute the nuclear factors */
        const long double N = screening[n];
        long double Sa[4] = {0, 0, 0, 0}, Sb[4] = {0, 0, 0, 0};
        for (i = 0; i < n; i++) {
                const long double Bi = screening[i];
                const long double x = 1. - Bi / N;
                long double r = x;
                for (j = 0; j < 4; j++) {
                        Sa[j] += a[i] / r;
                        r *= x;
                        Sb[j] += b[i] / r;
                }
        }

        long double tmp = 1;
        for (i = 0; i < 4; i++) {
                c[i] = ((4 - i) * Sb[3 - i] / N - Sa[3 - i]) * tmp;
                tmp *= N;
        }

        /* Update the pole factors due to nuclear terms */
        for (i = 0; i < n; i++) {
                const long double Bi = screening[i];
                long double d = 1. - Bi / N;
                d *= d;
                d *= d;
                a[i] = (a[i] - 4 * b[i] / (N - Bi)) / d;
                b[i] /= d;
        }
}

/**
 * Compute the restricted EHS cross-section in the CM frame.
 *
 * @param mu           The angular cut-off value.
 * @param fspin        The spin correction factors.
 * @param n_parameters The number of screening parameters.
 * @param screening    The screening parameters.
 * @param a            The 1st order pole coefficients.
 * @param b            The 2nd order pole coefficients.
 * @param c            The nuclear pole coefficients.
 *
 * The restricted cross-section is integrated from *mu* to `1`.
 */
double coulomb_restricted_cs(double mu, double fspin, int n_parameters,
    const double * screening, const long double * a, const long double * b,
    const long double * c)
{
        if ((mu >= 1.) || (mu >= 1E+06 * screening[n_parameters - 1]))
                return 0.;

        double coefficients[2];
        coulomb_transport_coefficients(1., fspin, n_parameters, screening,
            a, b, c, coefficients);
        double cs0 = coefficients[0];
        coulomb_transport_coefficients(mu, fspin, n_parameters, screening,
            a, b, c, coefficients);

        if (coefficients[0] > cs0)
                return 0;
        else
                return cs0 - coefficients[0];
}

/**
 * Compute the order 0 and 1 transport coefficients.
 *
 * @param mu           The angular cut-off.
 * @param fspin        The spin factor.
 * @param n_parameters The number of screening parameters.
 * @param screening    The screening factors.
 * @param a            The poles 1st order coefficients.
 * @param b            The poles 2nd order coefficients.
 * @param c            The poles nuclear coefficients.
 * @param coefficient  The computed transport coefficients.
 */
void coulomb_transport_coefficients(double mu_, double fspin, int n_parameters,
    const double * screening, const long double * a, const long double * b,
    const long double * c, double * coefficient)
{
        /* Sum up all factors of the pole reduction.
         *
         * Note: we make use of sum(a_i) + c_0 = 0 in order to eliminate some
         * unstable terms from the summation.
         */
        const long double S = fspin;
        const long double mu = mu_;

        const int n = n_parameters - 1;
        double cs0 = 0., cs1 = 0.;
        int i;
        for (i = 0; i < n; i++) {
                const long double alp = screening[i];
                const long double r = mu / (mu + alp);
                const long double L = logl(1. + mu / alp);
                const long double I0 = r / alp;
                const long double J0 = L;
                const long double I1 = L - r;
                const long double J1 = -alp * L;
                const long double I2 = mu + alp * (r - 2. * L);
                const long double J2 = alp * (alp * L - mu);

                cs0 += a[i] * (J0 - S * J1) + b[i] * (I0 - S * I1);
                cs1 += a[i] * (J1 - S * J2) + b[i] * (I1 - S * I2);
        }

        const long double N = screening[n];
        const long double r = mu / (mu + N);
        const long double L = logl(1. + mu / N);
        const long double I0 = r / N;
        const long double J0 = L;
        const long double I1 = L - r;
        const long double J1 = -N * L;
        const long double I2 = mu + N * (r - 2. * L);
        const long double J2 = N * (N * L - mu);

        const long double rn = 1. / (1. + mu / N);
        const long double K0 = (1. - rn) * (1. + rn) / (2 * N * N);
        const long double L0 = (1. - rn) * ((1. + rn) * (1. + rn) - rn) /
            (3 * N * N * N);
        const long double K1 = I0 - N * K0;
        const long double L1 = K0 - N * L0;
        const long double K2 = I1 - N * K1;
        const long double L2 = K1 - N * L1;

        cs0 += c[0] * (J0 - S * J1) +
               c[1] * (I0 - S * I1) +
               c[2] * (K0 - S * K1) +
               c[3] * (L0 - S * L1);
        cs1 += c[0] * (J1 - S * J2) +
               c[1] * (I1 - S * I2) +
               c[2] * (K1 - S * K2) +
               c[3] * (L1 - S * L2);

        coefficient[0] = cs0;
        coefficient[1] = 2 * cs1;
}

/**
 * The 1st transport cross-section for multiple scattering on electronic
 * shells.
 *
 * @param ZoA      The target Z over A ratio.
 * @param I        The target mean excitation energy.
 * @param aS       The target electronic scaling parameter.
 * @param mass     The projectile rest mass.
 * @param kinetic  The projectile initiale kinetic energy.
 * @param nu       The projectile energy loss cutoff.
 * @return The inverse of the electronic 1st transport path length in kg/m^2.
 *
 * The contribution from atomic electronic shells is computed following
 * Salvat et al., NIMB316 (2013) 144-159, using small angle approximation for
 * close collisions and keeping only leading term for distant ones.
 */
double transverse_transport_electronic(double ZoA, double I, double aS,
    double mass, double kinetic, double nu)
{
        /* Soft close and distant interactions restricted to cutoff. */
        const double momentum2 = kinetic * (kinetic + 2. * mass);
        const double E = kinetic + mass;
        const double Wr = 2. * ELECTRON_MASS * momentum2 /
            (mass * mass + ELECTRON_MASS * (ELECTRON_MASS + 2. * E));
        const double Wmax = (nu > Wr) ? Wr : nu;

        const double beta2 = momentum2 / (E * E);
        const double J = log(aS * Wmax / I) - beta2 * Wmax / Wr +
            0.25 * Wmax * Wmax / (E * E) + 1 / aS;

        /* Electronic Bremsstrahlung correction. */
        const double gamma = E / mass;
        const double lQ = log(1. + 2. * Wr / ELECTRON_MASS);
        const double Delta =
            ALPHA_EM / (2 * M_PI) * (log(2. * gamma) - lQ / 3.) * lQ * lQ;

        /* First transport cross-section */
        return 2 * M_PI * ELECTRON_RADIUS * ELECTRON_RADIUS * ELECTRON_MASS *
            ELECTRON_MASS * AVOGADRO_NUMBER * ZoA /
            (beta2 * 1E-03 * momentum2) * (J + Delta);
}

/* Low level routine: helper function for recording a MC state. */
/**
 * Register a Monte-Carlo state.
 *
 * @param recorder     The recorder handle.
 * @param medium       The medium in which the particle is located.
 * @param event        The current stepping event.
 * @param state        The Monte-Carlo state to record.
 *
 * This routine adds the given state to the recorder's stack.
 */
void record_state(struct pumas_context * context, struct pumas_medium * medium,
    enum pumas_event event, struct pumas_state * state)
{
        /* Check for a user supplied recorder */
        struct pumas_recorder * recorder = context->recorder;
        if (recorder->record != NULL) {
                recorder->record(context, state, medium, event);
                return;
        }

        /* Do the default recording ... */
        struct frame_recorder * const rec =
            (struct frame_recorder * const)recorder;
        struct frame_stack * stack = rec->stack;
        struct pumas_frame * frame = NULL;

        if ((stack == NULL) || (stack->size < (int)sizeof(*frame))) {
                /* Allocate a new memory segment. */
                const int size = 4096;
                stack = allocate(size);
                if (stack == NULL) return;
                stack->size = size - sizeof(*stack);
                stack->frame = stack->frames;
                stack->next = rec->stack;
                rec->stack = stack;
        }

        /* Allocate the new frame in the stack. */
        frame = stack->frame++;
        stack->size -= sizeof(*frame);

        /* Link the new frame. */
        if (recorder->first == NULL)
                recorder->first = frame;
        else
                rec->last->next = frame;
        rec->last = frame;
        frame->next = NULL;
        frame->medium = medium;
        frame->event = event;
        memcpy(&(frame->state), state, sizeof(*state));
        recorder->length++;
}

/* Low level routine: utility function for memory alignment. */
/**
 * Compute the padded memory size.
 *
 * @param size     The requested memory size.
 * @param pad_size The memory padding size.
 * @return The padded memory size.
 *
 * The padded memory size is the smallest integer multiple of *pad_size* and
 * greater or equal to *size*. It allows to align memory addresses on multiples
 * of pad_size.
 */
int memory_padded_size(int size, int pad_size)
{
        int i = size / pad_size;
        if ((size % pad_size) != 0) i++;
        return i * pad_size;
}

/* Low level routine: error handling. */

/**
 * Utility function for formating errors.
 *
 * @param error_     The error data.
 * @param rc         The return code.
 * @param caller     The calling function from which to return.
 * @param file       The file where the error occured.
 * @param line       The line where the error occured.
 * @param message    A brief description of the error.
 * @return The return code is forwarded.
 */
static enum pumas_return error_format(struct error_context * error_,
    enum pumas_return rc, const char * file, int line, const char * format, ...)
{
        if (error_ == NULL) return rc;

        error_->code = rc;
        if ((s_error.handler == NULL) || (rc == PUMAS_RETURN_SUCCESS))
                return rc;

        /* Format the error message */
        const int n =
            snprintf(error_->message, ERROR_MSG_LENGTH, "{ %s [#%d], %s:%d } ",
                pumas_error_function(error_->function), rc, file, line);
        if (n < ERROR_MSG_LENGTH - 1) {
                va_list ap;
                va_start(ap, format);
                vsnprintf(
                    error_->message + n, ERROR_MSG_LENGTH - n, format, ap);
                va_end(ap);
        }

        return rc;
}

/**
 * Utility function for handling errors.
 *
 * @param ezrror_ The error data.
 * @return The return code is forwarded.
 */
static enum pumas_return error_raise(struct error_context * error_)
{
        if ((s_error.handler == NULL) || (error_->code == PUMAS_RETURN_SUCCESS))
                return error_->code;

        if (s_error.catch) {
                if (s_error.catch_error.code == PUMAS_RETURN_SUCCESS) {
                        memcpy(&s_error.catch_error, error_,
                            sizeof(s_error.catch_error));
                }
                return error_->code;
        }
        s_error.handler(error_->code, error_->function, error_->message);

        return error_->code;
}

/*
 * Low level routines: I/O and parsing.
 */
/**
 * Parse a dE/dX file.
 *
 * @param Physics     Handle for physics tables.
 * @param fid         The file handle.
 * @param material    The material index.
 * @param filename    The name of the curent file.
 * @param error_      The error data.
 * @return On succces `PUMAS_RETURN_SUCCESS`, otherwise `PUMAS_ERROR`.
 *
 * Parse a dE/dX data table in PDG text file format.
 */
enum pumas_return io_parse_dedx_file(struct pumas_physics * physics, FILE * fid,
    int material, const char * filename, struct error_context * error_)
{
        char * buffer = NULL;

        /* Skip the header. */
        int line = 0;
        int i;
        for (i = 0; i < physics->n_energy_loss_header; i++) {
                io_read_line(fid, &buffer, filename, line, error_);
                line++;
                if (error_->code != PUMAS_RETURN_SUCCESS) return error_->code;
        }

        /* Initialise the new table. */
        int row = 0;
        *table_get_T(physics, PUMAS_MODE_CSDA, material, row) = 0.;
        *table_get_T(physics, PUMAS_MODE_MIXED, material, row) = 0.;
        *table_get_K(physics, row) = 0.;
        *table_get_dE(physics, PUMAS_MODE_CSDA, material, row) = 0.;
        *table_get_dE(physics, PUMAS_MODE_MIXED, material, row) = 0.;
        *table_get_NI_el(physics, PUMAS_MODE_CSDA, material, row) = 0.;
        *table_get_NI_el(physics, PUMAS_MODE_MIXED, material, row) = 0.;
        *table_get_NI_in(physics, material, row) = 0.;
        *table_get_CS(physics, material, row) = 0.;
        int ip;
        for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                *table_get_CSf(physics, ip, material, row) = 0.;
                *table_get_CSn(physics, ip, material, row) = 0.;
        }
        row++;

        /* Scan the new table. */
        while (error_->code == PUMAS_RETURN_SUCCESS) {
                io_read_line(fid, &buffer, filename, line, error_);
                line++;
                if (error_->code != PUMAS_RETURN_SUCCESS) break;
                io_parse_dedx_row(
                    physics, buffer, material, &row, filename, line, error_);
        }

        if (error_->code != PUMAS_RETURN_SUCCESS) {
                if ((error_->code == PUMAS_RETURN_END_OF_FILE) || feof(fid))
                        error_->code = PUMAS_RETURN_SUCCESS;
        }
        if (error_->code != PUMAS_RETURN_SUCCESS) return error_->code;

        if (row != physics->n_energies)
                return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                    "inconsistent number of rows in energy loss tables [%s, %d "
                    "!= %d]",
                    filename, row, physics->n_energies);

        compute_regularise_del(physics, material);

        return error_->code;
}

/**
 * Parse a row of a dE/dX file.
 *
 * @param Physics  Handle for physics tables.
 * @param buffer   The read buffer containing the row data.
 * @param material The index of the material.
 * @param row      The index of the parsed row.
 * @param filename The name of the current file.
 * @param line     The line being processed.
 * @param error_   The error data.
 * @return On succees `PUMAS_RETURN_SUCCESS`, or `PUMAS_ERROR` otherwise.
 *
 * Parse a row of a dE/dX data table formated in PDG text file format.
 */
enum pumas_return io_parse_dedx_row(struct pumas_physics * physics,
    char * buffer, int material, int * row, const char * filename, int line,
    struct error_context * error_)
{
        /*
         * Skip the peculiar values since they differ from material to
         * material.
         */
        if ((strstr(buffer, "Minimum ionization") != NULL) ||
            (strstr(buffer, "critical energy") != NULL))
                return PUMAS_RETURN_SUCCESS;

        /* parse the new data line */
        double k, a, be, de, brems, pair, photo;
        int count = sscanf(buffer, "%lf %*f %lf %lf %lf %lf %lf %lf", &k, &a,
            &brems, &pair, &photo, &be, &de);
        if ((count != 7) || (*row >= physics->n_energies))
                return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                    "invalid data line [@%s:%d]", filename, line);

        /* Change units. MeV to GeV and MeV cm^2/g to GeV m^2/kg. */
        k *= 1E-03;
        a *= 1E-04;
        be *= 1E-04;
        de *= 1E-04;
        brems *= 1E-04;
        pair *= 1E-04;
        photo *= 1E-04;

        /* Check the consistency of kinetic values. */
        if (material == 0) {
                *table_get_K(physics, *row) = k;
        } else if (*table_get_K(physics, *row) != k)
                return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                    "inconsistent kinetic energy value [@%s:%d, %.5lE != "
                    "%.5lE]",
                    filename, line, *table_get_K(physics, *row), k);

        /* Compute the fractional contributions of the energy loss
         * processes to the CEL and to DELs.
         */
        static double * cel_table = NULL;
        if (material == 0) {
                /* Precompute the per element terms. */
                if ((cel_table = compute_cel_and_del(physics, *row)) == NULL)
                        return ERROR_REGISTER_MEMORY();
        }

        struct material_component * component = physics->composition[material];
        double frct_cel[] = { 0., 0., 0., 0. };
        double frct_cs[] = { 0., 0., 0., 0. };
        double straggling = 0.;
        int ic, ic0 = 0;
        for (ic = 0; ic < material; ic++) ic0 += physics->elements_in[ic];
        for (ic = ic0; ic < ic0 + physics->elements_in[material];
             ic++, component++) {
                int iel = component->element;
                const double w = component->fraction;
                /* Loop over processes. */
                int ip;
                for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                        const double f =
                            (*table_get_CSn(physics, ip, iel, *row)) * w;
                        *table_get_CSf(physics, ip, ic, *row) = f;
                        frct_cs[ip] += f;
                        frct_cel[ip] +=
                            *table_get_cel(physics, ip, iel, *row, cel_table) *
                            w;
                        straggling +=
                            *table_get_stg(physics, ip, iel, *row, cel_table) *
                            w;
                }
        }

        const double cel_max[] = { brems, pair, photo, a };
        double be_cel = 0., frct_cs_del = 0.;
        int ip;
        for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                be_cel +=
                    (frct_cel[ip] < cel_max[ip]) ? frct_cel[ip] : cel_max[ip];
                frct_cs_del += frct_cs[ip];
        }

        /* Normalise the fractional cross-sections terms. */
        if (frct_cs_del <= 0.) {
                for (ic = ic0; ic < ic0 + physics->elements_in[material]; ic++)
                        for (ip = 0; ip < N_DEL_PROCESSES; ip++)
                                *table_get_CSf(physics, ip, ic, *row) = 0.;
        } else {
                double sum_tot = 0.;
                for (ic = ic0; ic < ic0 + physics->elements_in[material]; ic++)
                        for (ip = 0; ip < N_DEL_PROCESSES; ip++)
                                sum_tot +=
                                    *table_get_CSf(physics, ip, ic, *row);
                double sum = 0.;
                for (ic = ic0; ic < ic0 + physics->elements_in[material]; ic++)
                        for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                                sum += *table_get_CSf(physics, ip, ic, *row);
                                *table_get_CSf(physics, ip, ic, *row) =
                                    sum / sum_tot;
                        }

                /* Protect against rounding errors. */
                ic = ic0 + physics->elements_in[material] - 1;
                ip = N_DEL_PROCESSES - 1;
                *table_get_CSf(physics, ip, ic, *row) = 1.;
        }

        /* Update the table values */
        const double de_cel = de - be_cel;

        /* If this is the last entry, save the energy loss values. */
        if (*row == physics->n_energies - 1) {
                const double etot = k + physics->mass;
                *table_get_a_max(physics, material) = a;
                *table_get_b_max(physics, PUMAS_MODE_CSDA, material) =
                    be / etot;
                *table_get_b_max(physics, PUMAS_MODE_MIXED, material) =
                    (be - be_cel) / etot;
        }

        /* End point statistics */
        *table_get_dE(physics, PUMAS_MODE_CSDA, material, *row) = de;
        *table_get_dE(physics, PUMAS_MODE_MIXED, material, *row) = de_cel;
        *table_get_Omega(physics, material, *row) = straggling;
        *table_get_CS(physics, material, *row) = frct_cs_del;

        /* Weighted integrands */
        const double dei = 1. / de_cel;
        *table_get_X(physics, PUMAS_MODE_MIXED, material, *row) = dei;
        *table_get_NI_in(physics, material, *row) = frct_cs_del * dei;

        (*row)++;
        return PUMAS_RETURN_SUCCESS;
}

/**
 * Read a line from a file.
 *
 * @param fid      The file handle.
 * @param buf      A pointer to the new line or `NULL` in case of faillure.
 * @param filename The file currently processed.
 * @param line     The current line in the file.
 * @param error_   The error data.
 * @return On success `PUMAS_RETURN_SUCCESS` otherwise an error code.
 *
 * This routine manages a dynamic buffer. If *fid* is not `NULL`, this routine
 * reads a new line and fills in a pointer to a string holding the read data.
 * Otherwise, if *fid* is `NULL` any memory previously allocated by the routine
 * is released.
 */
enum pumas_return io_read_line(FILE * fid, char ** buf, const char * filename,
    int line, struct error_context * error_)
{
        static int size = 0;
        static char * buffer = NULL;

        if (buf != NULL) *buf = NULL;
        if (fid == NULL) {
                /* Release the buffer memory. */
                deallocate(buffer);
                buffer = NULL;
                size = 0;
                return PUMAS_RETURN_SUCCESS;
        }

        if (buffer == NULL) {
                /* Allocate the buffer if not already done. */
                size = 2048;
                buffer = allocate(size * sizeof(*buffer));
                if (buffer == NULL) return ERROR_REGISTER_MEMORY();
        }

        /* Get a full line. */
        char * s = buffer;
        int to_read = size;
        for (;;) {
                const char endline1 = '\n';
                const char endline2 = '\r';
                const char * r = fgets(s, to_read, fid);
                if (r == NULL) return ERROR_REGISTER_EOF(filename);
                int n_read = strlen(s);
                if ((n_read >= to_read - 1) && (s[to_read - 2] != endline1) &&
                    (s[to_read - 2] != endline2)) {
                        size += 2048;
                        char * new_buffer =
                            reallocate(buffer, size * sizeof(*buffer));
                        if (new_buffer == NULL) {
                                return ERROR_REGISTER_MEMORY();
                        }
                        buffer = new_buffer;
                        s += to_read - 1;
                        to_read = 2049;
                        continue;
                }
                break;
        }

        *buf = buffer;
        return PUMAS_RETURN_SUCCESS;
}

/* Check for a split in a camel case name */
static int camel_split(const char c0, const char c1, const char c2)
{
        const int upper1 = isupper(c1);

        /* Check for (.)([A-Z][a-z]+) */
        const int lower2 = islower(c2);
        if (upper1 && lower2) return 1;

        /* Check for ([a-z0-9])([A-Z]) */
        const int b0 = islower(c0) || isdigit(c0);
        if (b0 && upper1) return 1;

        return 0;
}

/* Create a default filename from the material name.
 *
 * The material name is expected to use camel case. The filename is a snakified
 * version of the material name.
 */
static void set_dedx_filename(const char * name, char ** filename_ptr)
{
        /* Check the filename size */
        const int n0 = strlen(name);
        int i, n = n0;
        for (i = 1; i < n0; i++) {
                const char c = (i < n0 - 1) ? name[i + 1] : name[n0 - 1];
                if (camel_split(name[i - 1], name[i], c)) n++;
        }

        /* Allocate memory for the filename */
        char * filename = reallocate(*filename_ptr, n + 5);
        *filename_ptr = filename;

        /* Build the filename */
        filename[0] = tolower(name[0]);

        int j;
        for (i = 1, j = 1; i < n0; i++, j++) {
                const char c = (i < n0 - 1) ? name[i + 1] : name[n0 - 1];
                if (camel_split(name[i - 1], name[i], c)) {
                        filename[j] = '_';
                        j++;
                }
                filename[j] = tolower(name[i]);
        }

        filename[n + 0] = '.';
        filename[n + 1] = 't';
        filename[n + 2] = 'x';
        filename[n + 3] = 't';
        filename[n + 4] = 0x0;
}

/**
 * Parse the global settings from a MDF.
 *
 * @param Physics      Handle for physics tables.
 * @param mdf          The MDF handle.
 * @param dedx_path    The path to the energy loss table(s).
 * @param error_       The error data.
 * @return On success `PUMAS_RETURN_SUCCESS` otherwise an error code.
 */
enum pumas_return mdf_parse_settings(const struct pumas_physics * physics,
    struct mdf_buffer * mdf, const char * dedx_path,
    struct error_context * error_)
{
        /* Initialisation of settings. */
        mdf->n_energies = 0;
        mdf->n_energy_loss_header = -1;
        mdf->n_materials = 0;
        mdf->n_composites = 0;
        mdf->n_elements = 0;
        mdf->n_components = 0;
        mdf->max_components = 0;
        mdf->size_composite = 0;
        mdf->size_dedx_path = 0;
        mdf->size_elements_names = 0;
        mdf->size_materials_names = 0;

        /* Prepare the path to the first dedx file. */
        char * full_path = NULL;
        char * filename = NULL;
        int offset_dir, size_path, size_name = 0;
        mdf->size_dedx_path = strlen(dedx_path) + 1;
        if (mdf_format_path(dedx_path, mdf->mdf_path, &full_path, &offset_dir,
            &size_path, error_) != PUMAS_RETURN_SUCCESS)
                goto clean_and_exit;

        /* Prepare the index tables. */
        if (mdf_settings_index(MDF_INDEX_INITIALISE, 0, error_) !=
            PUMAS_RETURN_SUCCESS)
                goto clean_and_exit;

        /* Loop on the XML nodes. */
        const int pad_size = sizeof(*(physics->data));
        mdf->left = 0;
        mdf->line = 1;
        mdf->depth = MDF_DEPTH_EXTERN;
        struct mdf_node node;
        for (;;) {
                /* Get the next node. */
                mdf_get_node(mdf, &node, error_);
                /* Check the termination. */
                if (error_->code == PUMAS_RETURN_END_OF_FILE) {
                        if (mdf->depth == MDF_DEPTH_EXTERN) {
                                /* This is a normal terminations. */
                                error_->code = PUMAS_RETURN_SUCCESS;
                                break;
                        } else {
                                goto clean_and_exit;
                        }
                } else if (error_->code != PUMAS_RETURN_SUCCESS)
                        goto clean_and_exit;
                if (node.key == MDF_KEY_OTHER) continue;

                /* Process material nodes. */
                if (node.key == MDF_KEY_MATERIAL) {
                        if (node.head == 1) {
                                /* This a material head. */
                                mdf->n_materials++;
                                const int size = strlen(node.at1.name) + 1;
                                mdf->size_materials_names += size;

                                /* Copy the filename if first. */
                                if (filename == NULL) {
                                        if (node.at2.file == NULL) {
                                                set_dedx_filename(node.at1.name,
                                                    &filename);
                                                size_name = strlen(filename);
                                        } else {
                                                size_name =
                                                    strlen(node.at2.file);
                                                filename =
                                                    allocate(size_name + 1);
                                                if (filename == NULL) {
                                                        ERROR_REGISTER_MEMORY();
                                                        goto clean_and_exit;
                                                }
                                                strcpy(filename, node.at2.file);
                                        }
                                }

                                /* Update the names table and book a new index
                                 * table.
                                 */
                                if ((mdf_settings_name(size, 'M', node.at1.name,
                                         error_) != PUMAS_RETURN_SUCCESS) ||
                                    (mdf_settings_index(
                                         MDF_INDEX_WRITE_MATERIAL, 0, error_) !=
                                        PUMAS_RETURN_SUCCESS))
                                        goto clean_and_exit;
                        } else {
                                if (mdf->elements_in > mdf->max_components)
                                        mdf->max_components = mdf->elements_in;
                                /* Finalise the index table. */
                                if (mdf_settings_index(
                                        MDF_INDEX_FINALISE_MATERIAL,
                                        mdf->elements_in,
                                        error_) != PUMAS_RETURN_SUCCESS)
                                        goto clean_and_exit;
                        }
                }

                /* Process composite material nodes. */
                if (node.key == MDF_KEY_COMPOSITE) {
                        if (node.head == 1) {
                                /* This a material head. */
                                mdf->n_materials++;
                                mdf->n_composites++;
                                const int size = strlen(node.at1.name) + 1;
                                mdf->size_materials_names += size;

                                /* Book a new index table. */
                                if (mdf_settings_index(
                                        MDF_INDEX_INITIALISE_COMPOSITE,
                                        mdf->n_elements,
                                        error_) != PUMAS_RETURN_SUCCESS)
                                        goto clean_and_exit;
                        } else {
                                /* Analyse the index table. */
                                int elements_in = mdf_settings_index(
                                    MDF_INDEX_FINALISE_COMPOSITE, 0, error_);
                                mdf->n_components += elements_in;
                                if (elements_in > mdf->max_components)
                                        mdf->max_components = elements_in;
                                mdf->size_composite += memory_padded_size(
                                    sizeof(struct composite_material) +
                                        mdf->materials_in *
                                            sizeof(struct composite_component),
                                    pad_size);
                        }
                }

                /* Skip others pure closings. */
                if (node.head == 0) continue;

                /* Process the other head nodes. */
                else if (node.key == MDF_KEY_ELEMENT) {
                        mdf->n_elements++;
                        const int size = strlen(node.at1.name) + 1;
                        mdf->size_elements_names +=
                            memory_padded_size(size, pad_size);
                        if (mdf_settings_name(size, 'E', node.at1.name,
                                error_) != PUMAS_RETURN_SUCCESS)
                                goto clean_and_exit;
                } else if (node.key == MDF_KEY_ATOMIC_COMPONENT) {
                        mdf->n_components++;
                        int index =
                            mdf_settings_name(0, 'E', node.at1.name, error_);
                        if (index < 0) {
                                ERROR_VREGISTER(PUMAS_RETURN_UNKNOWN_ELEMENT,
                                    "unknown atomic element `%s' [@%s:%d]",
                                    node.at1.name, mdf->mdf_path, mdf->line);
                                goto clean_and_exit;
                        }
                        if (mdf_settings_index(MDF_INDEX_WRITE_MATERIAL, index,
                                error_) != PUMAS_RETURN_SUCCESS)
                                goto clean_and_exit;
                } else if (node.key == MDF_KEY_COMPOSITE_COMPONENT) {
                        /* Update the components count. */
                        mdf->materials_in++;

                        /* Update the index table. */
                        int index =
                            mdf_settings_name(0, 'M', node.at1.name, error_);
                        if (index < 0) {
                                ERROR_VREGISTER(PUMAS_RETURN_UNKNOWN_MATERIAL,
                                    "unknown material `%s' [@%s:%d]",
                                    node.at1.name, mdf->mdf_path, mdf->line);
                                goto clean_and_exit;
                        }
                        if (mdf_settings_index(MDF_INDEX_UPDATE_COMPOSITE,
                                index, error_) != PUMAS_RETURN_SUCCESS)
                                goto clean_and_exit;
                }
        }
        mdf->line = 0;

        /* Check the content .*/
        if (mdf->n_elements == 0) {
                /* There are no elements or materials. */
                ERROR_VREGISTER(PUMAS_RETURN_INCOMPLETE_FILE,
                    "no elements in MDF file `%s'", mdf->mdf_path);
                goto clean_and_exit;
        } else if (mdf->n_materials == 0) {
                /* There are no elements or materials. */
                ERROR_VREGISTER(PUMAS_RETURN_INCOMPLETE_FILE,
                    "no materials in MDF file `%s'", mdf->mdf_path);
                goto clean_and_exit;
        }

        /* Parse the kinetic data. */
        if (!mdf->dry_mode) {
                /* Format the full path to the 1st energy loss file. */
                const int size_new = offset_dir + size_name + 1;
                if (size_new > size_path) {
                        /* Get enough memory. */
                        char * new_name = reallocate(full_path, size_new);
                        if (new_name == NULL) {
                                ERROR_REGISTER_MEMORY();
                                goto clean_and_exit;
                        }
                        full_path = new_name;
                }
                strcpy(full_path + offset_dir, filename);

                mdf_parse_kinetic(mdf, full_path, -1, NULL, error_);
        } else
                mdf->n_energies = 1;

clean_and_exit:
        /* Free the temporary memory and return. */
        mdf_settings_name(-1, 0x0, NULL, error_);
        mdf_settings_index(MDF_INDEX_FREE, 0, error_);
        deallocate(filename);
        deallocate(full_path);
        return error_->code;
}

/**
 * Manage a temporary mapping of material indices.
 *
 * @param operation The operation to perform.
 * @param value     An optional value for the operation.
 * @param error_    The error data.
 * @return The return value depends on the operation.
 */
int mdf_settings_index(int operation, int value, struct error_context * error_)
{
        const int chunk_size = 1024;
        static int * buffer = NULL;
        static int total_size = 0;
        static int free_size = 0;
        static int n_elements = 0;

        if (operation == MDF_INDEX_FREE) {
                /* Free the temporary memory. */
                deallocate(buffer);
                buffer = NULL;
                total_size = 0;
                free_size = 0;
                return PUMAS_RETURN_SUCCESS;
        } else if (operation == MDF_INDEX_FINALISE_MATERIAL) {
                /* Finalise the material index table. */
                int * header = buffer + (total_size - free_size - value - 1);
                *header = value;
                return PUMAS_RETURN_SUCCESS;
        } else if (operation == MDF_INDEX_UPDATE_COMPOSITE) {
                /* Update the composite index table. */
                int i, *table = buffer;
                for (i = 0; i < value; i++) {
                        table += (*table) + 1;
                }
                const int n = *(table++);
                int * has_element =
                    buffer + (total_size - free_size - n_elements);
                for (i = 0; i < n; i++) {
                        const int j = table[i];
                        if (has_element[j] == 0) has_element[j]++;
                }
                return PUMAS_RETURN_SUCCESS;
        } else if (operation == MDF_INDEX_FINALISE_COMPOSITE) {
                /* Finalise the composite index table. */
                int i, n = 0;
                int * table = buffer + (total_size - free_size - n_elements);
                for (i = 0; i < n_elements; i++) n += *(table++);
                free_size += n_elements;
                return n;
        }

        int size = (operation == MDF_INDEX_INITIALISE_COMPOSITE) ? value : 1;
        if (size > free_size) {
                /* Reserve memory for the next item. */
                const int delta_size =
                    ((size - free_size) / chunk_size + 1) * chunk_size;
                total_size += delta_size;
                int * new_buffer =
                    reallocate(buffer, total_size * sizeof(*buffer));
                if (new_buffer == NULL) {
                        deallocate(buffer);
                        buffer = NULL;
                        total_size = 0;
                        free_size = 0;
                        return ERROR_REGISTER_MEMORY();
                }
                buffer = new_buffer;
                free_size += delta_size;
        }

        if (operation == MDF_INDEX_INITIALISE) {
                return PUMAS_RETURN_SUCCESS;
        } else if (operation == MDF_INDEX_WRITE_MATERIAL) {
                /* Write a value to the material index table. */
                int * tail = buffer + (total_size - free_size);
                *tail = value;
                free_size--;
                return PUMAS_RETURN_SUCCESS;
        } else if (operation == MDF_INDEX_INITIALISE_COMPOSITE) {
                /* Book a new index table for a composite material. */
                n_elements = value;
                int * tail = buffer + (total_size - free_size);
                memset(tail, 0x0, n_elements * sizeof(int));
                free_size -= n_elements;
                return PUMAS_RETURN_SUCCESS;
        }

        return ERROR_VREGISTER(
            PUMAS_RETURN_INDEX_ERROR, "invalid operation `%s'", operation);
}

/**
 * Manage a temporary table of material names.
 *
 * @param size   The operation to perform or the size of the new name.
 * @param prefix A prefix for the category of the name (base, composite, ...).
 * @param name   The name to handle.
 * @param error_ The error data.
 * @return The return value depends on the operation performed.
 */
int mdf_settings_name(
    int size, char prefix, const char * name, struct error_context * error_)
{
        const int chunk_size = 4096;
        static char * buffer = NULL;
        static int total_size = 0;
        static int free_size = 0;

        if (size == 0) {
                /* Return the name index. */
                if (buffer == NULL) return -1;
                const char * ptr = buffer;
                int index = 0;
                while (*ptr != 0x0) {
                        if (*ptr == prefix) {
                                if (strcmp(ptr + 1, name) == 0) return index;
                                index++;
                        }
                        ptr += strlen(ptr + 1) + 2;
                }
                return -1;
        } else if (size < 0) {
                /* Free the temporary memory. */
                deallocate(buffer);
                buffer = NULL;
                total_size = 0;
                free_size = 0;
                return PUMAS_RETURN_SUCCESS;
        }

        size += 1;
        if (size + 1 > free_size) {
                /* Reserve memory for the next item. */
                const int delta_size =
                    ((size + 1 - free_size) / chunk_size + 1) * chunk_size;
                total_size += delta_size;
                char * new_buffer = reallocate(buffer, total_size);
                if (new_buffer == NULL) {
                        deallocate(buffer);
                        buffer = NULL;
                        total_size = 0;
                        free_size = 0;
                        return ERROR_REGISTER_MEMORY();
                }
                buffer = new_buffer;
                free_size += delta_size;
        }
        if (buffer == NULL) return -1;

        /* Dump the new item. */
        char * tail = buffer + (total_size - free_size);
        tail[0] = prefix;
        tail++;
        strcpy(tail, name);
        free_size -= size;
        *(tail + size - 1) = 0x0; /* Tag the end of the name list. */
        return PUMAS_RETURN_SUCCESS;
}

/**
 * Parse the kinetic energy values from a dE/dX file.
 *
 * @param mdf           The MDF handle.
 * @param path          The full path to the dE/dX file.
 * @param n_energies    The number of kinetic energy values.
 * @param energy        The (read) energy values or `NULL`.
 * @param error_        The error data.
 * @return On success `PUMAS_RETURN_SUCCESS` otherwise `PUMAS_ERROR`.
 */
enum pumas_return mdf_parse_kinetic(struct mdf_buffer * mdf, const char * path,
    int n_energies, double * energy, struct error_context * error_)
{
        /* Initialise the settings. */
        mdf->n_energies = 0;
        mdf->line = 0;

        /* Open the dedx file. */
        FILE * fid = fopen(path, "r");
        if (fid == NULL) {
                return ERROR_VREGISTER(
                    PUMAS_RETURN_PATH_ERROR, "could not open file `%s'", path);
        }

        /* Skip the header lines. */
        char * buffer = NULL;
        for (mdf->n_energy_loss_header = 0;; mdf->n_energy_loss_header++) {
                io_read_line(fid, &buffer, path, mdf->line, error_);
                mdf->line++;
                if (error_->code != PUMAS_RETURN_SUCCESS) {
                        fclose(fid);
                        return error_->code;
                }
                const char * c;
                int i;
                for (i = 0, c = buffer; (*c == ' ') && (i < 4); i++) c++;
                if (isdigit(*c)) break;
        }

        /* Scan the table. */
        int nk = 1;
        for (;;) {
                /* Check for a comment. */
                if (strstr(buffer, "Minimum ionization") != NULL)
                        goto next_line;
                if (strstr(buffer, "critical energy") != NULL) goto next_line;

                /* parse the new data line. */
                double k;
                if (sscanf(buffer, "%lf", &k) <= 0) goto next_line;
                k *= 1E-03;
                if (energy != NULL) {
                        if (n_energies > 0) {
                                if ((nk > n_energies) ||
                                    (k != energy[nk - 1])) {
                                        ERROR_VREGISTER(
                                            PUMAS_RETURN_FORMAT_ERROR,
                                            "bad format for file `%s'", path);
                                        break;
                                }
                        } else {
                                energy[nk - 1] = k;
                        }
                }
                nk++;

        next_line:
                /* Check for a new line. */
                io_read_line(fid, &buffer, path, mdf->line, error_);
                mdf->line++;
                if (error_->code != PUMAS_RETURN_SUCCESS) {
                        if ((error_->code == PUMAS_RETURN_END_OF_FILE) ||
                            feof(fid)) {
                                error_->code = PUMAS_RETURN_SUCCESS;
                                break;
                        }
                }
        }
        io_read_line(NULL, NULL, NULL, 0, error_);

        /*  Update the settings. */
        if (error_->code == PUMAS_RETURN_SUCCESS) mdf->line = 0;
        mdf->n_energies = nk;

        fclose(fid);
        return error_->code;
}

/**
 * Parse the atomic elements from a MDF.
 *
 * @param Physics   Handle for physics tables.
 * @param mdf       The MDF handle.
 * @param error     The error data.
 * @return On success `PUMAS_RETURN_SUCCESS` otherwise `PUMAS_ERROR`.
 */
enum pumas_return mdf_parse_elements(const struct pumas_physics * physics,
    struct mdf_buffer * mdf, struct error_context * error_)
{
        /* Loop on the XML nodes. */
        rewind(mdf->fid);
        mdf->left = 0;
        mdf->line = 1;
        mdf->depth = MDF_DEPTH_EXTERN;
        struct mdf_node node;
        const int pad_size = sizeof(*(physics->data));

        int iel = 0;
        for (;;) {
                /* Get the next node. */
                if (mdf_get_node(mdf, &node, error_) != PUMAS_RETURN_SUCCESS)
                        break;

                if (node.key != MDF_KEY_ELEMENT)
                        continue;
                else if ((node.head == 0) && (node.tail == 1)) {
                        if (iel >= physics->n_elements)
                                break;
                        else
                                continue;
                }

                /* Set the element data. */
                if (iel == 0) {
                        char * tmp = (char *)physics->element +
                            memory_padded_size(physics->n_elements *
                                             sizeof(physics->element[0]),
                                         pad_size);
                        physics->element[0] = (struct atomic_element *)tmp;
                } else {
                        const struct atomic_element * e =
                            physics->element[iel - 1];
                        physics->element[iel] =
                            (struct atomic_element *)((char *)(e) +
                                sizeof(*e) +
                                memory_padded_size(strlen(e->name) + 1,
                                                          pad_size));
                }
                struct atomic_element * e = physics->element[iel];
                e->name = (char *)(e->data);
                strcpy(e->name, node.at1.name);
                if ((sscanf(node.at2.Z, "%lf", &(e->Z)) != 1) || (e->Z <= 0.)) {
                        ERROR_VREGISTER(PUMAS_RETURN_VALUE_ERROR,
                            "invalid atomic number `Z' [%s]", node.at2.Z);
                        break;
                }
                if ((sscanf(node.at3.A, "%lf", &(e->A)) != 1) || (e->A <= 0.)) {
                        ERROR_VREGISTER(PUMAS_RETURN_VALUE_ERROR,
                            "invalid atomic mass `A' [%s]", node.at3.A);
                        break;
                }
                if ((sscanf(node.at4.I, "%lf", &(e->I)) != 1) || (e->I <= 0.)) {
                        ERROR_VREGISTER(PUMAS_RETURN_VALUE_ERROR,
                            "invalid Mean Excitation Energy `I' [%s]",
                            node.at4.I);
                        break;
                }
                e->I *= 1E-09;

                /* Increment. */
                e->index = iel;
                iel++;
                if ((node.tail == 1) && (iel >= physics->n_elements)) break;
        }

        return error_->code;
}

/**
 * Parse the base materials from a MDF.
 *
 * @param Physics   Handle for physics tables.
 * @param mdf       The MDF handle.
 * @param error_    The error data
 * @return On success `PUMAS_RETURN_SUCCESS` otherwise `PUMAS_ERROR`.
 *
 * The materials dE/dX data are loaded according to the path provided
 * by the `<table>` XML node. If path is not absolute, it specifies a relative
 * path to the directory where the MDF is located.
 */
enum pumas_return mdf_parse_materials(struct pumas_physics * physics,
    struct mdf_buffer * mdf, struct error_context * error_)
{
        /* Format the path directory name. */
        char * filename = NULL;
        int offset_dir, size_name;
        if (!mdf->dry_mode) {
                if ((mdf_format_path(physics->dedx_path, physics->mdf_path,
                        &filename, &offset_dir, &size_name, error_)) !=
                    PUMAS_RETURN_SUCCESS)
                        return error_->code;
        }

        /* Loop on the XML nodes. */
        rewind(mdf->fid);
        mdf->left = 0;
        mdf->line = 1;
        mdf->depth = 0;
        struct mdf_node node;
        int imat = 0;
        const int pad_size = sizeof(*(physics->data));
        void * tmp_ptr = ((char *)physics->composition) +
            memory_padded_size(physics->n_materials *
                                 sizeof(struct material_component *),
                             pad_size);
        struct material_component * data = tmp_ptr;

        for (;;) {
                /* Get the next node. */
                mdf_get_node(mdf, &node, error_);
                if (error_->code != PUMAS_RETURN_SUCCESS) break;

                /* Set the material data. */
                if (node.key == MDF_KEY_MATERIAL) {
                        if (node.head == 0) {
                                /* This is a material closing. */
                                physics->elements_in[imat] = mdf->elements_in;

                                /* Compute the relative electron density. */
                                compute_ZoA(physics, imat);

                                /* Check for dry mode. */
                                if (mdf->dry_mode) goto update_count;

                                /* Read the energy loss data. */
                                FILE * fid = fopen(filename, "r");
                                if (fid == NULL) {
                                        ERROR_VREGISTER(PUMAS_RETURN_PATH_ERROR,
                                            "could not open file `%s'",
                                            filename);
                                        break;
                                }
                                io_parse_dedx_file(
                                    physics, fid, imat, filename, error_);
                                fclose(fid);
                                if (error_->code != PUMAS_RETURN_SUCCESS) break;

                        /* Update the material count. */
                        update_count:
                                imat++;
                                if (imat >= physics->n_materials -
                                        physics->n_composites)
                                        break;
                                continue;
                        }

                        /* We have a new material opening. */
                        if (imat == 0) {
                                physics->material_name[0] =
                                    (char *)(physics->material_name +
                                        physics->n_materials);
                        } else {
                                physics->material_name[imat] =
                                    physics->material_name[imat - 1] +
                                    strlen(physics->material_name[imat - 1]) +
                                    1;
                        }
                        strcpy(physics->material_name[imat], node.at1.name);
                        physics->composition[imat] = data;

                        /* Format the energy loss filename. */
                        if (!mdf->dry_mode) {
                                if (node.at2.file == NULL) {
                                        if (physics->dedx_filename[imat] ==
                                            NULL) {
                                                set_dedx_filename(node.at1.name,
                                                    physics->dedx_filename +
                                                    imat);
                                        }
                                        node.at2.file =
                                            physics->dedx_filename[imat];
                                }
                                const int size_new =
                                    offset_dir + strlen(node.at2.file) + 1;
                                if (size_new > size_name) {
                                        /* Get enough memory. */
                                        char * new_name =
                                            reallocate(filename, size_new);
                                        if (new_name == NULL) {
                                                ERROR_REGISTER_MEMORY();
                                                break;
                                        }
                                        filename = new_name;
                                        size_name = size_new;
                                }
                                strcpy(filename + offset_dir, node.at2.file);
                        } else {
                                if (node.at2.file == NULL) {
                                        if (physics->dedx_filename[imat] ==
                                            NULL) {
                                                set_dedx_filename(node.at1.name,
                                                    physics->dedx_filename +
                                                imat);
                                        }
                                        node.at2.file =
                                            physics->dedx_filename[imat];
                                } else {
                                        int n = strlen(node.at2.file) + 1;
                                        physics->dedx_filename[imat] =
                                            allocate(n);
                                        if (physics->dedx_filename[imat] ==
                                            NULL) {
                                                ERROR_REGISTER_MEMORY();
                                                break;
                                        }
                                        memcpy(physics->dedx_filename[imat],
                                            node.at2.file, n);
                                }
                        }

                        /* Parse the default density. */
                        double rho;
                        if ((sscanf(node.at3.density, "%lf", &rho) != 1)
                            || (rho <= 0.)) {
                                ERROR_VREGISTER(
                                    PUMAS_RETURN_VALUE_ERROR,
                                    "invalid value for density [%s]",
                                   node.at3.density);
                                break;
                        }
                        rho *= 1E+03; /* g/cm^3 -> kg/m^3 */
                        physics->material_density[imat] = rho;

                        /* Parse the MEE. */
                        double I = 0.;
                        if (node.at4.I != NULL) {
                                if ((sscanf(node.at4.I, "%lf", &I) != 1)
                                    || (I <= 0.)) {
                                        ERROR_VREGISTER(
                                            PUMAS_RETURN_VALUE_ERROR,
                                            "invalid value for I [%s]",
                                           node.at4.I);
                                        break;
                                }
                                I *= 1E-09; /* eV to GeV */
                        }
                        physics->material_I[imat] = I;
                }

                /* Skip other closings. */
                if (node.head == 0) continue;

                /* Set the composition data. */
                if (node.key == MDF_KEY_ATOMIC_COMPONENT) {
                        int i = mdf->elements_in - 1;
                        int iel = element_index(physics, node.at1.name);
                        if (iel < 0) return PUMAS_RETURN_UNKNOWN_ELEMENT;
                        physics->composition[imat][i].element = iel;

                        double f;
                        if ((sscanf(node.at2.fraction, "%lf", &f) != 1) ||
                            (f <= 0.)) {
                                ERROR_VREGISTER(PUMAS_RETURN_VALUE_ERROR,
                                    "invalid value for mass fraction [%s]",
                                    node.at2.fraction);
                                break;
                        }
                        physics->composition[imat][i].fraction = f;
                        data++;
                }
        }

        if (error_->code != PUMAS_RETURN_SUCCESS) {
                /* Clear the filenames, whenever allocated. */
                int i;
                for (i = 0; i < physics->n_materials - physics->n_composites;
                     i++) {
                        deallocate(physics->dedx_filename[imat]);
                        physics->dedx_filename[imat] = NULL;
                }
        }

        deallocate(filename);
        return error_->code;
}

/**
 * Parse the composite materials from a MDF.
 *
 * @param Physics   Handle for physics tables.
 * @param mdf       The MDF handle.
 * @return On success `PUMAS_RETURN_SUCCESS` otherwise an error code.
 *
 * The composite materials properties are given as a linear combination
 * of the base material ones.
 */
enum pumas_return mdf_parse_composites(struct pumas_physics * physics,
    struct mdf_buffer * mdf, struct error_context * error_)
{
        /* Loop on the XML nodes. */
        rewind(mdf->fid);
        mdf->left = 0;
        mdf->line = 1;
        mdf->depth = 0;
        struct mdf_node node;
        int icomp = 0;
        int elements_in = 0;
        const int imat = physics->n_materials - physics->n_composites - 1;
        struct material_component * data_el =
            (struct material_component *)((char *)(physics->composition[imat]) +
                physics->elements_in[imat] * sizeof(struct material_component));
        const int pad_size = sizeof(*(physics->data));
        void * tmp_ptr = ((char *)physics->composite) +
            memory_padded_size(physics->n_composites *
                                 sizeof(struct composite_material *),
                             pad_size);
        struct composite_material * data_ma = tmp_ptr;

        for (;;) {
                /* Get the next node. */
                if (mdf_get_node(mdf, &node, error_) != PUMAS_RETURN_SUCCESS)
                        break;

                /* Set the material data. */
                if (node.key == MDF_KEY_COMPOSITE) {
                        const int i0 = imat + icomp + 1;

                        if (node.head == 0) {
                                /* This is a composite closing. */
                                data_ma->n_components = mdf->materials_in;
                                physics->elements_in[i0] = elements_in;
                                data_ma =
                                    (struct composite_material *)((char *)
                                                                      data_ma +
                                        sizeof(struct composite_material) +
                                        mdf->materials_in *
                                            sizeof(struct composite_component));
                                data_el += elements_in;

                                /* update the composite material count. */
                                icomp++;
                                if (icomp >= physics->n_composites) break;
                                continue;
                        }

                        /* We have a new material opening. */
                        physics->material_name[i0] =
                            physics->material_name[i0 - 1] +
                            strlen(physics->material_name[i0 - 1]) + 1;
                        strcpy(physics->material_name[i0], node.at1.name);
                        physics->composition[i0] = data_el;
                        physics->composite[icomp] = data_ma;
                        elements_in = 0;
                }

                /* Skip other closings. */
                if (node.head == 0) continue;

                /* Set the composition data. */
                if (node.key == MDF_KEY_COMPOSITE_COMPONENT) {
                        int i = mdf->materials_in - 1;
                        int ima = 0;
                        if (material_index(physics, node.at1.name, &ima,
                                error_) != PUMAS_RETURN_SUCCESS)
                                break;
                        data_ma->component[i].material = ima;
                        double f;
                        if ((sscanf(node.at2.fraction, "%lf", &f) != 1) ||
                            (f < 0.)) {
                                ERROR_VREGISTER(PUMAS_RETURN_VALUE_ERROR,
                                    "invalid mass fraction [%s]",
                                    node.at2.fraction);
                                break;
                        }
                        data_ma->component[i].fraction = f;

                        const int i0 = imat + icomp + 1;
                        for (i = 0; i < physics->elements_in[ima]; i++) {
                                int iel = physics->composition[ima][i].element;
                                int j, n = elements_in;
                                int already_listed = 0;
                                for (j = 0; j < n; j++) {
                                        if (iel ==
                                            physics->composition[i0]
                                                                [j].element) {
                                                already_listed = 1;
                                                break;
                                        }
                                }
                                if (already_listed != 0) continue;
                                physics->composition[i0][elements_in++]
                                    .element = iel;
                        }
                }
        }

        if (error_->code == PUMAS_RETURN_END_OF_FILE)
                error_->code = PUMAS_RETURN_SUCCESS;
        return error_->code;
}

/**
 * Get the next XML node in the MDF.
 *
 * @param mdf    The MDF handle.
 * @param node   The node handle.
 * @param error_ The error data.
 * @return On success `PUMAS_RETURN_SUCCES` otherwise the corresponding
 * error code.
 *
 * Search the file for the next valid XML node. On success, at return the *node*
 * attributes are updated with links to the XML buffer. The parsing enforces
 * consistency of the `<pumas>` node and children. Openings and closures
 * above are not checked, neither the tag names.
 */
enum pumas_return mdf_get_node(struct mdf_buffer * mdf, struct mdf_node * node,
    struct error_context * error_)
{
        /* Initialise the node. */
        node->at1.name = node->at2.Z = node->at3.A = node->at4.I = NULL;
        node->key = -1;
        node->head = node->tail = 0;

        for (;;) {
                /* Load data if empty buffer. */
                if (mdf->left < 1) {
                        mdf->left = fread(
                            mdf->data, sizeof(char), mdf->size - 1, mdf->fid);
                        if (mdf->left <= 0)
                                return ERROR_REGISTER_EOF(mdf->mdf_path);
                        mdf->pos = mdf->data;
                        *(mdf->data + mdf->left) = '\0';
                }

                /* Locate a node start tag. */
                char * start = mdf->pos;
                while (*start != '<') {
                        if (*start == '\0')
                                break;
                        else if (*start == '\n') {
                                if (start[1] != '\r') mdf->line++;
                        } else if (*start == '\r') {
                                if (start[1] != '\n') mdf->line++;
                        }
                        start++;
                }
                if (*start == '\0') {
                        mdf->left = 0;
                        continue;
                }
                mdf->left -= start - mdf->pos;
                mdf->pos = start;

                /* Relocate the node to the start of the buffer. */
                memmove(mdf->data, mdf->pos, mdf->left);
                mdf->left += fread(mdf->data + mdf->left, sizeof(char),
                    mdf->size - 1 - mdf->left, mdf->fid);
                if (mdf->left <= 1) return ERROR_REGISTER_EOF(mdf->mdf_path);
                mdf->pos = mdf->data;
                *(mdf->data + mdf->left) = '\0';

                /* Parse the node name. */
                char * key = mdf->pos + 1;
                char * tail = key;
                if (strncmp(key, "!--", 3) == 0) {
                        /* We have a comment block. */
                        tail = key + 3;
                } else {
                        /* We have a regular node. */
                        while ((*tail != ' ') && (*tail != '>')) {
                                if (*tail == '\0')
                                        return ERROR_REGISTER_TOO_LONG(
                                            mdf->mdf_path, mdf->line);
                                tail++;
                        }
                }
                char tailler = *tail;
                *tail = '\0';
                tail++;
                mdf->left -= tail - mdf->pos;
                mdf->pos = tail;

                /* Check if this is a comment. */
                if (strcmp(key, "!--") == 0) {
                        enum pumas_return rc =
                            mdf_skip_pattern(mdf, "-->", error_);
                        if (rc != PUMAS_RETURN_SUCCESS) return rc;
                        continue;
                }

                /* Check if we have a head or tail key. */
                if (*key == '/') {
                        node->tail = 1;
                        key++;
                } else {
                        node->head = 1;
                }

                /* Check the node key. */
                node->key = MDF_KEY_OTHER;
                if (mdf->depth == MDF_DEPTH_EXTERN) {
                        if (strcmp(key, "pumas") == 0)
                                node->key = MDF_KEY_PUMAS;
                        else
                                node->key = MDF_KEY_OTHER;
                } else if (mdf->depth == MDF_DEPTH_ROOT) {
                        if (strcmp(key, "pumas") == 0)
                                node->key = MDF_KEY_PUMAS;
                        else if (strcmp(key, "element") == 0)
                                node->key = MDF_KEY_ELEMENT;
                        else if (strcmp(key, "material") == 0)
                                node->key = MDF_KEY_MATERIAL;
                        else if (strcmp(key, "composite") == 0)
                                node->key = MDF_KEY_COMPOSITE;
                        else
                                return ERROR_REGISTER_UNEXPECTED_TAG(
                                    key, mdf->mdf_path, mdf->line);
                } else if (mdf->depth == MDF_DEPTH_ELEMENT) {
                        if (strcmp(key, "element") == 0)
                                node->key = MDF_KEY_ELEMENT;
                        else
                                return ERROR_REGISTER_UNEXPECTED_TAG(
                                    key, mdf->mdf_path, mdf->line);
                } else if (mdf->depth == MDF_DEPTH_MATERIAL) {
                        if (strcmp(key, "material") == 0)
                                node->key = MDF_KEY_MATERIAL;
                        else if (strcmp(key, "component") == 0)
                                node->key = MDF_KEY_ATOMIC_COMPONENT;
                        else
                                return ERROR_REGISTER_UNEXPECTED_TAG(
                                    key, mdf->mdf_path, mdf->line);
                } else if (mdf->depth == MDF_DEPTH_COMPOSITE) {
                        if (strcmp(key, "composite") == 0)
                                node->key = MDF_KEY_COMPOSITE;
                        else if (strcmp(key, "component") == 0)
                                node->key = MDF_KEY_COMPOSITE_COMPONENT;
                        else
                                return ERROR_REGISTER_UNEXPECTED_TAG(
                                    key, mdf->mdf_path, mdf->line);
                } else
                        return ERROR_REGISTER_UNEXPECTED_TAG(
                            key, mdf->mdf_path, mdf->line);

                /* Check if we have an empty node. */
                if (tailler == '>')
                        goto consistency_check;
                else if ((node->head == 0) && (node->tail == 1)) {
                        /* We have a badly finished pure tailler. */
                        return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                            "unmatched XML closer [@%s:%d]", mdf->mdf_path,
                            mdf->line);
                }

                /* Parse any attributes. */
                for (;;) {
                        char * sep = mdf->pos;
                        while ((*sep != '=') && (*sep != '>')) {
                                if (*sep == '\0')
                                        return ERROR_REGISTER_TOO_LONG(
                                            mdf->mdf_path, mdf->line);
                                sep++;
                        }
                        if (*sep == '>') {
                                /* The end tag was reached. */
                                if ((*(sep - 1) == '/') || (*(sep - 1) == '?'))
                                        node->tail = 1;
                                sep++;
                                mdf->left -= sep - mdf->pos;
                                mdf->pos = sep;
                                goto consistency_check;
                        }

                        /* Parse the attribute and its value. */
                        char * attr = mdf->pos;
                        while (*attr == ' ') attr++;
                        char * end = attr + 1;
                        while ((*end != ' ') && (*end != '=')) end++;
                        *end = '\0';

                        char * value = strchr(sep + 1, '"');
                        if (value == NULL)
                                return ERROR_REGISTER_INVALID_XML_VALUE(
                                    sep + 1, mdf->mdf_path, mdf->line);
                        value++;
                        end = strchr(value, '"');
                        if (end == NULL)
                                return ERROR_REGISTER_INVALID_XML_VALUE(
                                    value, mdf->mdf_path, mdf->line);
                        *end = '\0';

                        /* Map the value to the attribute. */
                        if (node->key == MDF_KEY_PUMAS) {
                                return PUMAS_RETURN_FORMAT_ERROR;
                        } else if (node->key == MDF_KEY_ELEMENT) {
                                if (strcmp(attr, "name") == 0)
                                        node->at1.name = value;
                                else if (strcmp(attr, "Z") == 0)
                                        node->at2.Z = value;
                                else if (strcmp(attr, "A") == 0)
                                        node->at3.A = value;
                                else if (strcmp(attr, "I") == 0)
                                        node->at4.I = value;
                                else
                                        return ERROR_REGISTER_INVALID_XML_ATTRIBUTE(
                                            attr, "<element>", mdf->mdf_path,
                                            mdf->line);
                        } else if (node->key == MDF_KEY_MATERIAL) {
                                if (strcmp(attr, "name") == 0)
                                        node->at1.name = value;
                                else if (strcmp(attr, "file") == 0)
                                        node->at2.file = value;
                                else if (strcmp(attr, "density") == 0)
                                        node->at3.density = value;
                                else if (strcmp(attr, "I") == 0)
                                        node->at4.I = value;
                                else
                                        return ERROR_REGISTER_INVALID_XML_ATTRIBUTE(
                                            attr, "<material>", mdf->mdf_path,
                                            mdf->line);
                        } else if (node->key == MDF_KEY_COMPOSITE) {
                                if (strcmp(attr, "name") == 0)
                                        node->at1.name = value;
                                else
                                        return ERROR_REGISTER_INVALID_XML_ATTRIBUTE(
                                            attr, "<composite>", mdf->mdf_path,
                                            mdf->line);
                        } else if (node->key == MDF_KEY_ATOMIC_COMPONENT) {
                                if (strcmp(attr, "name") == 0)
                                        node->at1.name = value;
                                else if (strcmp(attr, "fraction") == 0)
                                        node->at2.fraction = value;
                                else
                                        return ERROR_REGISTER_INVALID_XML_ATTRIBUTE(
                                            attr, "<component>", mdf->mdf_path,
                                            mdf->line);
                        } else if (node->key == MDF_KEY_COMPOSITE_COMPONENT) {
                                if (strcmp(attr, "name") == 0)
                                        node->at1.name = value;
                                else if (strcmp(attr, "fraction") == 0)
                                        node->at2.fraction = value;
                                else
                                        return ERROR_REGISTER_INVALID_XML_ATTRIBUTE(
                                            attr, "<component>", mdf->mdf_path,
                                            mdf->line);
                        }

                        /* Update the buffer cursor. */
                        end++;
                        mdf->left -= end - mdf->pos;
                        mdf->pos = end;
                }
        }

consistency_check:
        /* Update the depth. */
        if (mdf->depth == MDF_DEPTH_EXTERN) {
                if (node->key == MDF_KEY_PUMAS) {
                        if (node->tail == 1)
                                return ERROR_VREGISTER(
                                    PUMAS_RETURN_FORMAT_ERROR,
                                    "invalid <pumas> XML element [@%s:%d]",
                                    mdf->mdf_path, mdf->line);
                        mdf->depth = MDF_DEPTH_ROOT;
                }
        } else if (mdf->depth == MDF_DEPTH_ROOT) {
                if (node->key == MDF_KEY_PUMAS) {
                        if (node->head == 1)
                                return ERROR_VREGISTER(
                                    PUMAS_RETURN_FORMAT_ERROR,
                                    "nested <pumas> XML element [@%s:%d]",
                                    mdf->mdf_path, mdf->line);
                        mdf->depth = MDF_DEPTH_EXTERN;
                } else if (node->key == MDF_KEY_ELEMENT) {
                        if (node->tail == 0) mdf->depth = MDF_DEPTH_ELEMENT;
                } else if (node->key == MDF_KEY_MATERIAL) {
                        if (node->tail == 1)
                                return ERROR_VREGISTER(
                                    PUMAS_RETURN_FORMAT_ERROR,
                                    "invalid <material> XML element [@%s:%d]",
                                    mdf->mdf_path, mdf->line);
                        mdf->depth = MDF_DEPTH_MATERIAL;
                } else if (node->key == MDF_KEY_COMPOSITE) {
                        if (node->tail == 1)
                                return ERROR_VREGISTER(
                                    PUMAS_RETURN_FORMAT_ERROR,
                                    "invalid <composite> XML element [@%s:%d]",
                                    mdf->mdf_path, mdf->line);
                        mdf->depth = MDF_DEPTH_COMPOSITE;
                }
        } else if (mdf->depth == MDF_DEPTH_ELEMENT) {
                if (node->key == MDF_KEY_ELEMENT) {
                        if (node->head == 1)
                                return ERROR_VREGISTER(
                                    PUMAS_RETURN_FORMAT_ERROR,
                                    "nested <element> XML element [@%s:%d]",
                                    mdf->mdf_path, mdf->line);
                        mdf->depth = MDF_DEPTH_ROOT;
                }
        } else if (mdf->depth == MDF_DEPTH_MATERIAL) {
                if (node->key == MDF_KEY_MATERIAL) {
                        if (node->head == 1)
                                return ERROR_VREGISTER(
                                    PUMAS_RETURN_FORMAT_ERROR,
                                    "nested <material> XML element [@%s:%d]",
                                    mdf->mdf_path, mdf->line);
                        mdf->depth = MDF_DEPTH_ROOT;
                }
        } else if (mdf->depth == MDF_DEPTH_COMPOSITE) {
                if (node->key == MDF_KEY_COMPOSITE) {
                        if (node->head == 1)
                                return ERROR_VREGISTER(
                                    PUMAS_RETURN_FORMAT_ERROR,
                                    "nested <composite> XML element [@%s:%d]",
                                    mdf->mdf_path, mdf->line);
                        mdf->depth = MDF_DEPTH_ROOT;
                }
        } else
                return PUMAS_RETURN_FORMAT_ERROR;

        /* Check that the node has all its attributes defined. */
        if (node->head == 0) {
                if ((node->key == MDF_KEY_MATERIAL) && (mdf->elements_in == 0))
                        return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                            "missing <element>(s) for XML <material> [@%s:%d]",
                            mdf->mdf_path, mdf->line);
                else if ((node->key == MDF_KEY_COMPOSITE) &&
                    (mdf->materials_in == 0))
                        return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                            "missing <material>(s) for XML <composite> "
                            "[@%s:%d]",
                            mdf->mdf_path, mdf->line);
                return PUMAS_RETURN_SUCCESS;
        }

        if (node->key == MDF_KEY_COMPOSITE) {
                if (node->at1.name == NULL)
                        return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                            "missing attribute(s) for XML <composite> [@%s:%d]",
                            mdf->mdf_path, mdf->line);
        } else if ((node->key == MDF_KEY_ATOMIC_COMPONENT) ||
            (node->key == MDF_KEY_COMPOSITE_COMPONENT)) {
                if ((node->at1.name == NULL) || (node->at2.fraction == NULL))
                        return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                            "missing attribute(s) for XML <component> [@%s:%d]",
                            mdf->mdf_path, mdf->line);
        } else if (node->key == MDF_KEY_MATERIAL) {
                if ((node->at1.name == NULL) || (node->at3.density == NULL))
                        return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                            "missing attribute(s) for XML <component> [@%s:%d]",
                            mdf->mdf_path, mdf->line);
        } else if (node->key == MDF_KEY_ELEMENT) {
                if ((node->at1.name == NULL) || (node->at2.Z == NULL) ||
                    (node->at3.A == NULL) || (node->at4.I == NULL))
                        return ERROR_VREGISTER(PUMAS_RETURN_FORMAT_ERROR,
                            "missing attribute(s) for XML <element> [@%s:%d]",
                            mdf->mdf_path, mdf->line);
        }

        /* Update analysis data. */
        if (node->key == MDF_KEY_MATERIAL) {
                mdf->elements_in = 0;
        } else if (node->key == MDF_KEY_COMPOSITE) {
                mdf->materials_in = 0;
        } else if (node->key == MDF_KEY_ATOMIC_COMPONENT) {
                mdf->elements_in++;
        } else if (node->key == MDF_KEY_COMPOSITE_COMPONENT) {
                mdf->materials_in++;
        }

        return PUMAS_RETURN_SUCCESS;
}

/**
 * Skip chars in MDF up to and including a given pattern.
 *
 * @param mdf     The MDF handle.
 * @param pattern The pattern to skip.
 * @param error_  The error data.
 * @return On success `PUMAS_RETURN_SUCCES` otherwise the corresponding
 * error code.
 *
 * Search the file for *pattern* and skip it. This routine is used in
 * order to skip XML comments.
 */
enum pumas_return mdf_skip_pattern(struct mdf_buffer * mdf,
    const char * pattern, struct error_context * error_)
{
        const int pattern_size = strlen(pattern);
        for (;;) {
                if (mdf->left < pattern_size) {
                        memmove(mdf->data, mdf->pos, mdf->left);
                        mdf->left += fread(mdf->data + mdf->left, sizeof(char),
                            mdf->size - 1 - mdf->left, mdf->fid);
                        if (mdf->left < pattern_size) {
                                return ERROR_REGISTER_EOF(mdf->mdf_path);
                        }
                        mdf->pos = mdf->data;
                        *(mdf->data + mdf->left) = '\0';
                }

                char * p = mdf->pos;
                while (*p != '\0') {
                        if (*p == '\n') {
                                if (p[1] != '\r') mdf->line++;
                        } else if (*p == '\r') {
                                if (p[1] != '\n') mdf->line++;
                        }
                        if (strncmp(p, pattern, pattern_size) == 0) break;
                        p++;
                }

                if (*p != '\0') {
                        p += pattern_size;
                        mdf->left -= p - mdf->pos;
                        mdf->pos = p;
                        return PUMAS_RETURN_SUCCESS;
                }
                mdf->pos += mdf->left - pattern_size + 1;
                mdf->left = pattern_size - 1;
        }
}

/**
 * Format the path for a dE/dX data file.
 *
 * @param directory The base directory where the dE/dX file is located.
 * @param filename  The formated path for the filename.
 * @param offset    The offset to the file name part of the path.
 * @param size_name The total memory size available for the path string.
 * @param error_    The error data.
 * @return On success `PUMAS_RETURN_SUCCESS` otherwise `PUMAS_ERROR`.
 *
 * The full path to the filename string is formated by prepending the
 * relative or absolute directory path. If directory is a relative path,
 * the directory of the MDF is used as root.
 */
enum pumas_return mdf_format_path(const char * directory, const char * mdf_path,
    char ** filename, int * offset_dir, int * size_name,
    struct error_context * error_)
{
        /* Format the path directory name. */
        const char sep = '/';
        const char altsep =
#if (defined _WIN32) || (defined WIN32) || (defined __CYGWIN__)
            '\\';
#else
            '/';
#endif
        const int initial_size = 128;
        if (directory[0] != '@') {
                /* We have an absolute path name. */
                const int dir_size = strlen(directory);
                *offset_dir = dir_size + 1;
                *size_name = (*offset_dir) + initial_size;
                char * tmp = reallocate(*filename, *size_name);
                if (tmp == NULL) {
                        return ERROR_REGISTER_MEMORY();
                }
                *filename = tmp;
                strcpy(*filename, directory);
                (*filename)[(*offset_dir) - 1] = altsep;
        } else {
                /* We have a relative path name. */
                const int dir_size = strlen(++directory);
                int n1 = strlen(mdf_path) - 1;
                while ((n1 >= 0) && (mdf_path[n1] != sep)
                    && (mdf_path[n1] != altsep)) n1--;
                *offset_dir = n1 + dir_size + 2;
                *size_name = (*offset_dir) + initial_size;
                char * tmp = reallocate(*filename, *size_name);
                if (tmp == NULL) {
                        return ERROR_REGISTER_MEMORY();
                }
                *filename = tmp;
                if (n1 > 0) strncpy(*filename, mdf_path, n1 + 1);
                strcpy((*filename) + n1 + 1, directory);
                (*filename)[(*offset_dir) - 1] = altsep;
        }

        return PUMAS_RETURN_SUCCESS;
}

/*
 * Low level routines: pre-computation of various properties.
 */
/**
 * Precompute the integrals for a deterministic CEL and TT parameters, for
 * composite materials.
 *
 * @param Physics  Handle for physics tables.
 * @param material The composite material index.
 * @param error_   The error data.
 * @return `PUMAS_ERROR` if any computation failled, `PUMAS_RETURN_SUCCESS`
 * otherwise.
 */
enum pumas_return compute_composite(
    struct pumas_physics * physics, int material, struct error_context * error_)
{
        compute_composite_weights(physics, material);
        compute_composite_tables(physics, material);
        compute_regularise_del(physics, material);
        compute_cel_integrals(physics, material);
        compute_csda_magnetic_transport(physics, material);
        return compute_scattering(physics, material, error_);
}

/**
 * Compute various cumulative integrals for a deterministic CEL.
 *
 * @param Physics  Handle for physics tables.
 * @param material The index of the material to tabulate.
 */
void compute_cel_integrals(struct pumas_physics * physics, int material)
{
        compute_pchip_coeffs(physics, material);
        compute_cel_grammage_integral(physics, 0, material);
        compute_cel_grammage_integral(physics, 1, material);
        compute_time_integrals(physics, material);
        compute_kinetic_integral(physics,
            table_get_NI_in(physics, material, 0),
            table_get_NI_in_dK(physics, material, 0));
        compute_pchip_integral_coeffs(physics, material);
}

/**
 * Compute various quantities related to scattering.
 *
 * @param Physics  Handle for physics tables.
 * @param material The index of the material to tabulate.
 */
enum pumas_return compute_scattering(
    struct pumas_physics * physics, int material, struct error_context * error_)
{
        int ikin;
        for (ikin = 0; ikin < physics->n_energies; ikin++) {
                const enum pumas_return rc = compute_scattering_parameters(
                    physics, material, ikin, error_);
                if (rc != PUMAS_RETURN_SUCCESS) return rc;
        }
        compute_pchip_scattering_coeffs(physics, material);
        compute_kinetic_integral(physics,
            table_get_NI_el(physics, PUMAS_MODE_CSDA, material, 0),
            table_get_NI_el_dK(physics, PUMAS_MODE_CSDA, material, 0));
        compute_kinetic_integral(physics,
            table_get_NI_el(physics, PUMAS_MODE_MIXED, material, 0),
            table_get_NI_el_dK(physics, PUMAS_MODE_MIXED, material, 0));
        compute_pchip_scattering_integral_coeffs(physics, material);
        return PUMAS_RETURN_SUCCESS;
}

/**
 * Computation of mixture atomic weights for composite materials.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index of the composite to compute.
 */
void compute_composite_weights(struct pumas_physics * physics, int material)
{
        const int icomp =
            material - physics->n_materials + physics->n_composites;
        int i;
        for (i = 0; i < physics->elements_in[material]; i++) {
                struct material_component * component =
                    physics->composition[material] + i;
                component->fraction = 0.;
        }

        for (i = 0; i < physics->composite[icomp]->n_components; i++) {
                struct composite_component * component =
                    physics->composite[icomp]->component + i;
                const int imat = component->material;
                int j;
                for (j = 0; j < physics->elements_in[imat]; j++) {
                        const struct material_component * cij =
                            physics->composition[imat] + j;
                        int k;
                        for (k = 0; k < physics->elements_in[material]; k++) {
                                struct material_component * c =
                                    physics->composition[material] + k;
                                if (c->element == cij->element) {
                                        c->fraction +=
                                            component->fraction * cij->fraction;
                                        break;
                                }
                        }
                }
        }

        /* Compute the relative electron density and MEE. */
        compute_ZoA(physics, material);
        compute_MEE(physics, material);
}

/**
 * Compute the density of a composite material.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index of the composite material.
 * @param error    The error data.
 * @return `PUMAS_ERROR` if any density is not strictly positive,
 * `PUMAS_RETURN_SUCCESS` otherwise.
 */
enum pumas_return compute_composite_density(
    struct pumas_physics * physics, int material, struct error_context * error_)
{
        const int icomp =
            material - physics->n_materials + physics->n_composites;
        int i;
        double rho_inv = 0., nrm = 0.;
        for (i = 0; i < physics->composite[icomp]->n_components; i++) {
                const struct composite_component * component =
                    physics->composite[icomp]->component + i;
                const double component_density =
                    physics->material_density[component->material];
                if (component_density <= 0.)
                        return ERROR_REGISTER_NEGATIVE_DENSITY(
                            physics->material_name[component->material]);
                rho_inv += component->fraction / component_density;
                nrm += component->fraction;
        }

        if (rho_inv <= 0.)
                return ERROR_REGISTER_NEGATIVE_DENSITY(
                    physics->material_name[material]);
        physics->material_density[material] = nrm / rho_inv;
        return PUMAS_RETURN_SUCCESS;
}

/**
 * Computation of tabulated properties for composite materials.
 *
 * @param Physics  Handle for physics tables.
 * @param material The matrial index of the composite to compute.
 */
void compute_composite_tables(struct pumas_physics * physics, int material)
{
        const int icomp =
            material - physics->n_materials + physics->n_composites;

        /* Initialise to zero. */
        int i, row, k0 = 0;
        for (i = 0; i < material; i++) k0 += physics->elements_in[i];
        row = 0;
        *table_get_T(physics, PUMAS_MODE_CSDA, material, row) = 0.;
        *table_get_T(physics, PUMAS_MODE_MIXED, material, row) = 0.;
        *table_get_NI_in(physics, material, row) = 0.;
        for (row = 0; row < physics->n_energies; row++) {
                *table_get_dE(physics, PUMAS_MODE_CSDA, material, row) = 0.;
                *table_get_dE(physics, PUMAS_MODE_MIXED, material, row) = 0.;
                *table_get_Omega(physics, material, row) = 0.;
                *table_get_CS(physics, material, row) = 0.;
                int k, ip;
                for (k = 0; k < physics->elements_in[material]; k++)
                        for (ip = 0; ip < N_DEL_PROCESSES; ip++)
                                *table_get_CSf(physics, ip, k0 + k, row) = 0.;
        }
        *table_get_Kt(physics, material) = 0.;
        *table_get_a_max(physics, material) = 0.;
        *table_get_b_max(physics, PUMAS_MODE_CSDA, material) = 0.;
        *table_get_b_max(physics, PUMAS_MODE_MIXED, material) = 0.;

        /* End point statistics */
        for (i = 0; i < physics->composite[icomp]->n_components; i++) {
                struct composite_component * component =
                    physics->composite[icomp]->component + i;
                const int imat = component->material;
                const double kt = *table_get_Kt(physics, imat);

                /* Total cross section and energy loss. */
                for (row = 0; row < physics->n_energies; row++) {
                        *table_get_dE(physics, 0, material, row) +=
                            *table_get_dE(physics, 0, imat, row) *
                            component->fraction;
                        *table_get_dE(physics, 1, material, row) +=
                            *table_get_dE(physics, 1, imat, row) *
                            component->fraction;
                        *table_get_Omega(physics, material, row) +=
                            *table_get_Omega(physics, imat, row) *
                            component->fraction;
                        const double k = *table_get_K(physics, row);
                        const double cs =
                            (k < kt) ? 0. : *table_get_CS(physics, imat, row) *
                                component->fraction;
                        *table_get_CS(physics, material, row) += cs;
                }

                /* Fractional contribution to the cross section. */
                int j, j0 = 0;
                for (j = 0; j < imat; j++) j0 += physics->elements_in[j];

                for (row = 0; row < physics->n_energies; row++) {
                        const double kinetic = *table_get_K(physics, row);
                        if (kinetic < kt) continue;

                        const double cs_tot = *table_get_CS(physics, imat, row);
                        double csf_last = 0.;
                        for (j = 0; j < physics->elements_in[imat]; j++) {
                                /* Locate the element. */
                                const struct material_component * cij =
                                    physics->composition[imat] + j;
                                int k;
                                for (k = 0; k < physics->elements_in[material];
                                     k++) {
                                        struct material_component * c =
                                            k + physics->composition[material];
                                        if (c->element == cij->element) break;
                                }

                                /* Update the fractional cross-section. */
                                int ip;
                                for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                                        const double csf = *table_get_CSf(
                                            physics, ip, j0 + j, row);
                                        *table_get_CSf(physics, ip, k0 + k,
                                            row) += cs_tot * (csf - csf_last) *
                                            component->fraction;
                                        csf_last = csf;
                                }
                        }
                }

                /* Maximum tabulated energy loss parameters. */
                *table_get_a_max(physics, material) +=
                    *table_get_a_max(physics, imat) * component->fraction;
                *table_get_b_max(physics, 0, material) +=
                    *table_get_b_max(physics, 0, imat) * component->fraction;
                *table_get_b_max(physics, 1, material) +=
                    *table_get_b_max(physics, 1, imat) * component->fraction;
        }

        /* Normalise the fractional contributions to the cross section. */
        for (row = 0; row < physics->n_energies; row++) {
                int k, ip;
                const double cs = *table_get_CS(physics, material, row);
                if (cs <= 0.) {
                        for (k = 0; k < physics->elements_in[material]; k++)
                                for (ip = 0; ip < N_DEL_PROCESSES; ip++)
                                        *table_get_CSf(
                                            physics, ip, k0 + k, row) = 0.;
                        continue;
                }

                double sum = 0.;
                for (k = 0; k < physics->elements_in[material]; k++)
                        for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                                sum += *table_get_CSf(physics, ip, k0 + k, row);
                                const double f = sum / cs;
                                *table_get_CSf(physics, ip, k0 + k, row) =
                                    (f > 1.) ? 1. : f;
                        }

                /* Protect against rounding errors. */
                k = physics->elements_in[material] - 1;
                *table_get_CSf(physics, N_DEL_PROCESSES - 1, k0 + k, row) = 1.;
        }

        /* Weighted integrands */
        for (row = 1; row < physics->n_energies; row++) {
                *table_get_NI_in(physics, material, row) =
                    *table_get_CS(physics, material, row) /
                    *table_get_dE(physics, PUMAS_MODE_MIXED, material, row);
        }
}

/**
 * Compute the cumulative integral of a table column.
 *
 * @param Physics   Handle for physics tables.
 * @param table     The table column to process.
 *
 * Compute a cumulative integral of the column by integrating over the kinetic
 * energy with a cubic interpolation.
 */
void compute_kinetic_integral(
    struct pumas_physics * physics, double * table, double * work)
{
        const int n = physics->n_energies;
        const double * x = table_get_K(physics, 0);
        math_pchip_integrate(n, x, table, work);
}

/**
 * Compute cumulative integrals for the proper time.
 *
 * @param Physics   Handle for physics tables.
 * @param table     The table column to process.
 *
 * Compute the proper time cumulative integrals over path length with a
 * trapezoidal rule.
 */
void compute_time_integrals(struct pumas_physics * physics, int material)
{
        static double I0 = 0.;
        if (I0 == 0.) {
                /* Compute the integral of 1/momemtum for the lowest energy bin
                 * using trapezes. */
                const int n = 101;
                int i;
                const double dK = (*table_get_K(physics, 1)) / (n - 1);
                double Ki = dK;
                I0 = 0.5 / sqrt(Ki * (Ki + 2. * physics->mass));
                for (i = 2; i < n - 1; i++) {
                        Ki += dK;
                        const double pi = sqrt(Ki * (Ki + 2. * physics->mass));
                        I0 += 1. / pi;
                }
                Ki += dK;
                I0 += 0.5 / sqrt(Ki * (Ki + 2. * physics->mass));
                I0 /= n - 1;
        }

        /* Compute the cumulative path integrals . */
        double * const K = table_get_K(physics, 0);
        double * const T0 =
            table_get_T(physics, PUMAS_MODE_CSDA, material, 0);
        double * const T1 =
            table_get_T(physics, PUMAS_MODE_MIXED, material, 0);
        double * const X0 =
            table_get_X(physics, PUMAS_MODE_CSDA, material, 0);
        double * const X1 =
            table_get_X(physics, PUMAS_MODE_MIXED, material, 0);

        T0[0] = T1[0] = 0.;
        T0[1] = I0 * X0[1] * physics->mass;
        T1[1] = I0 * X1[1] * physics->mass;
        int i;
        for (i = 2; i < physics->n_energies; i++) {
                const double p0 =
                    sqrt(K[i - 1] * (K[i - 1] + 2. * physics->mass));
                const double p1 = sqrt(K[i] * (K[i] + 2. * physics->mass));
                const double psi = 1. / p0 + 1. / p1;
                const double dy0 = 0.5 * (X0[i] - X0[i - 1]) * psi;
                const double dy1 = 0.5 * (X1[i] - X1[i - 1]) * psi;
                T0[i] = T0[i - 1] + dy0 * physics->mass;
                T1[i] = T1[i - 1] + dy1 * physics->mass;
        }
}

/**
 * Compute the CSDA grammage range from the energy loss.
 *
 * @param Physics   Handle for physics tables.
 *  @param scheme   The index of the simulation scheme.
 *  @param material The index of the material to process.
 *
 * Compute the cumulative CSDA grammage integral from the energy loss
 * using a cubic interpolation.
 */
void compute_cel_grammage_integral(
    struct pumas_physics * physics, int scheme, int material)
{
        double * y = table_get_X(physics, scheme, material, 0);
        const double * const dEdX = table_get_dE(physics, scheme, material, 0);
        const int n = physics->n_energies;
        int i;
        for (i = 1; i < n; i++) {
                y[i] = 1 / dEdX[i];
        }
        y[0] = 0;

        const double * x = table_get_K(physics, 0);
        double * d = table_get_X_dK(physics, scheme, material, 0);
        math_pchip_integrate(n, x, y, d);
}

/* Evaluate the derivative at x0 using 3 neighbouring values */
double math_diff3(
    double x0, double x1, double x2, double y0, double y1, double y2)
{
        const double h1 = x1 - x0;
        const double h2 = x2 - x0;
        const double delta = h1 * h2 * (h2 - h1);
        const double c1 = h2 * h2 / delta;
        const double c2 = -h1 * h1 / delta;
        const double c0 = -(c1 + c2);
        return c0 * y0 + c1 * y1 + c2 * y2;
}

/** Compute the derivative coefficients for the PCHIP interpolation
 *
 * @param der  Flag indicating if the true derivative is known.
 * @param n    The number of nodes.
 * @param x    The x values of the interpolation nodes.
 * @param y    The y values of the interpolation nodes.
 * @param m    The derivative coefficients.
 *
 * The derivative cooefficients are computed using the method of Fritsch and
 * Butland. For boundary conditions a 3 points finite difference is used.
 *
 * Between the first and second entry a linear interpolation is used instead
 * because the function is extrapolated.
 *
 * References:
 *  F. N. Fristch and J. Butland, SIAM J. Sci. Stat. Comput. (1984)
 */
void math_pchip_initialise(
    int der, int n, const double * x, const double * y, double * m)
{
        if (n >= 3) {
                /* Linear interpolation for the extrapolated bin */
                m[0] = (y[1] - y[0]) / (x[1] - x[0]);
                n--;
                x++;
                y++;
                m++;
        }

        if (n == 1) {
                if (!der) m[0] = 0;
        } else if (n == 2) {
                if (!der) {
                        const double d = (y[1] - y[0]) / (x[1] - x[0]);
                        m[0] = m[1] = d;
                }
        } else {
                int i;
                for (i = 1; i < n - 1; i++) {
                        const double h1 = x[i] - x[i - 1];
                        if (der) {
                                const double a = m[i] / h1;
                                const double b = m[i + 1] / h1;
                                const double c = 2 * a + b - 3.;
                                if (3 * a * (a + b - 2.) >= c * c) continue;
                        }

                        const double h2 = x[i + 1] - x[i];
                        const double S1 = (y[i] - y[i - 1]) / h1;
                        const double S2 = (y[i + 1] - y[i]) / h2;

                        const double tmp = S1 * S2;
                        if (tmp > 0) {
                                const double a =
                                    (h1 + 2 * h2) / (3 * (h1 + h2));
                                m[i] = tmp / ((1 - a) * S1 + a * S2);
                        } else {
                                m[i] = 0.;
                        }
                }

                if (!der) {
                        m[0] = math_diff3(x[0], x[1], x[2], y[0], y[1], y[2]);
                        m[n - 1] = math_diff3(x[n - 1], x[n - 2], x[n - 3],
                            y[n - 1], y[n - 2], y[n - 3]);
                }
        }
}

/** Compute a cumulative integral using PCHIP
 *
 * @param n    The number of nodes.
 * @param x    The x values of the interpolation nodes.
 * @param y    The y values of the interpolation nodes.
 * @param d    The derivative coefficients.
 *
 * The derivative cooefficients are computed using `math_pchip_initialise`.
 * The integral is updated in-place.
 */
void math_pchip_integrate(
    int n, const double * x, double * y, double * d)
{
        math_pchip_initialise(0, n, x, y, d);

        /* Linear interpolation for the first bin */
        double I = 0.5 * (x[1] - x[0]) * (y[1] + y[0]);
        y[0] = 0.;

        /* Cubic interpolation for other bins */
        int i;
        for (i = 1; i < n - 1; i++) {
                const double x0 = x[i];
                const double x1 = x[i + 1];
                const double dx = x1 - x0;
                const double p0 = y[i];
                const double p1 = y[i + 1];
                const double m0 = d[i] * dx;
                const double m1 = d[i + 1] * dx;

                const double a0 = p0;
                const double a1 = 0.5 * m0;
                const double a2 = -(3 * (p0 - p1) + 2 * m0 + m1) / 3.;
                const double a3 = 0.25 * (2 * (p0 - p1) + m0 + m1);

                y[i] = I;
                I += dx * (a0 + a1 + a2 + a3);
        }
        y[n - 1] = I;
}

/**
 * Compute the derivative PCHIP coefficients for base material quantities.
 *
 * @param Physics   Handle for physics tables.
 *  @param scheme   The index of the simulation scheme.
 *  @param material The index of the material to process.
 *
 * Between the first and second entry a linear interpolation is used instead
 * because the function is no more monotone.
 */
void compute_pchip_coeffs(
    struct pumas_physics * physics, int material)
{
        const int n = physics->n_energies;
        const double * const K = table_get_K(physics, 0);

        math_pchip_initialise(0, n, K,
            table_get_Omega(physics, material, 0),
            table_get_Omega_dK(physics, material, 0));

        math_pchip_initialise(0, n, K,
            table_get_CS(physics, material, 0),
            table_get_CS_dK(physics, material, 0));

        int scheme;
        for (scheme = PUMAS_MODE_CSDA; scheme <= PUMAS_MODE_MIXED; scheme++) {
                math_pchip_initialise(0, n, K,
                    table_get_dE(physics, scheme, material, 0),
                    table_get_dE_dK(physics, scheme, material, 0));
        }

        int i;
        for (i = 0; i < N_LARMOR_ORDERS + 1; i++) {
                math_pchip_initialise(0, n, K,
                    table_get_Li(physics, material, i, 0),
                    table_get_Li_dK(physics, material, i, 0));
        }
}

/**
 * Compute the derivative PCHIP coefficients for atomic elements quantities.
 *
 * @param Physics   Handle for physics tables.
 *  @param scheme   The index of the simulation scheme.
 *
 * Between the first and second entry a linear interpolation is used instead
 * because the function is no more monotone.
 */
void compute_pchip_elements_coeffs(struct pumas_physics * physics)
{
        const int n = physics->n_energies;
        const double * const K = table_get_K(physics, 0);

        int i;
        for (i = 0; i < N_DEL_PROCESSES; i++) {
                int j;
                for (j = 0; j < physics->n_components; j++) {
                        math_pchip_initialise(0, n, K,
                            table_get_CSf(physics, i, j, 0),
                            table_get_CSf_dK(physics, i, j, 0));
                }

                for (j = 0; j < physics->n_elements; j++) {
                        math_pchip_initialise(0, n, K,
                            table_get_CSn(physics, i, j, 0),
                            table_get_CSn_dK(physics, i, j, 0));
                }
        }
}

/**
 * Compute the derivative PCHIP coefficients for integral quantities.
 *
 * @param Physics   Handle for physics tables.
 *  @param scheme   The index of the simulation scheme.
 *  @param material The index of the material to process.
 *
 * Between the first and second entry a linear interpolation is used instead
 * because the function is no more monotone.
 */
void compute_pchip_integral_coeffs(
    struct pumas_physics * physics, int material)
{
        const int n = physics->n_energies;
        const double * const K = table_get_K(physics, 0);
        const int dif = 1;

        {
                int i;
                const double * dE =
                    table_get_dE(physics, PUMAS_MODE_MIXED, material, 0);
                const double * CS =
                    table_get_CS(physics, material, 0);
                double * m;

                m = table_get_K_dNI_in(physics, material, 0);
                for (i = 0; i < n; i++) m[i] = (CS[i] > 0) ? dE[i] / CS[i] : 0.;
                math_pchip_initialise(
                    dif, n, table_get_NI_in(physics, material, 0), K, m);

                m = table_get_NI_in_dK(physics, material, 0);
                m[0] = 0.;
                for (i = 1; i < n; i++) m[i] = (dE[i] > 0) ? CS[i] / dE[i] : 0.;
                math_pchip_initialise(
                    dif, n, K, table_get_NI_in(physics, material, 0), m);
        }

        int scheme;
        for (scheme = PUMAS_MODE_CSDA; scheme <= PUMAS_MODE_MIXED; scheme++) {
                int i;
                const double mass = physics->mass;
                const double * dE =
                    table_get_dE(physics, scheme, material, 0);
                double * m;

                m = table_get_K_dX(physics, scheme, material, 0);
                for (i = 0; i < n; i++) m[i] = dE[i];
                math_pchip_initialise(
                    dif, n, table_get_X(physics, scheme, material, 0), K, m);

                m = table_get_X_dK(physics, scheme, material, 0);
                m[0] = 0.;
                for (i = 1; i < n; i++) m[i] = 1. / dE[i];
                math_pchip_initialise(
                    dif, n, K, table_get_X(physics, scheme, material, 0), m);

                math_pchip_initialise(0, n,
                    table_get_T(physics, scheme, material, 0),
                    table_get_X(physics, scheme, material, 0),
                    table_get_X_dT(physics, scheme, material, 0));

                m = table_get_T_dK(physics, scheme, material, 0);
                m[0] = 0.;
                for (i = 1; i < n; i++) m[i] = mass /
                    (dE[i] * sqrt(K[i] * (K[i] + 2 * mass)));
                math_pchip_initialise(
                    dif, n, K, table_get_T(physics, scheme, material, 0), m);
        }
}

/**
 * Compute the derivative PCHIP coefficients for scattering related quantities.
 *
 * @param Physics   Handle for physics tables.
 *  @param scheme   The index of the simulation scheme.
 *  @param material The index of the material to process.
 *
 * Between the first and second entry a linear interpolation is used instead
 * because the function is no more monotone.
 */
void compute_pchip_scattering_coeffs(
    struct pumas_physics * physics, int material)
{
        const int n = physics->n_energies;
        const double * const K = table_get_K(physics, 0);

        math_pchip_initialise(0, n, K,
            table_get_Mu0(physics, material, 0),
            table_get_Mu0_dK(physics, material, 0));

        math_pchip_initialise(0, n, K,
            table_get_Lb(physics, material, 0),
            table_get_Lb_dK(physics, material, 0));

        int scheme;
        for (scheme = PUMAS_MODE_DISABLED; scheme <= PUMAS_MODE_MIXED;
            scheme++) {
                math_pchip_initialise(0, n, K,
                    table_get_Ms1(physics, scheme, material, 0),
                    table_get_Ms1_dK(physics, scheme, material, 0));
        }
}

/**
 * Compute the derivative PCHIP coefficients for scattering integral quantities.
 *
 * @param Physics   Handle for physics tables.
 *  @param scheme   The index of the simulation scheme.
 *  @param material The index of the material to process.
 *
 * Between the first and second entry a linear interpolation is used instead
 * because the function is no more monotone.
 */
void compute_pchip_scattering_integral_coeffs(
    struct pumas_physics * physics, int material)
{
        const int n = physics->n_energies;
        const double * const K = table_get_K(physics, 0);
        const int dif = 1;

        int scheme;
        for (scheme = PUMAS_MODE_CSDA; scheme <= PUMAS_MODE_MIXED; scheme++) {
                int i;
                const double mass = physics->mass;
                const double * dE =
                    table_get_dE(physics, scheme, material, 0);
                const double * Lb = table_get_Lb(physics, material, 0);
                double * m;

                m = table_get_NI_el_dK(physics, scheme, material, 0);
                m[0] = 0.;
                for (i = 1; i < n; i++) m[i] = K[i] * (K[i] + 2 * mass) /
                    (dE[i] * Lb[i]);
                math_pchip_initialise(dif, n, K,
                    table_get_NI_el(physics, scheme, material, 0), m);

                m = table_get_K_dNI_el(physics, scheme, material, 0);
                m[0] = 0.;
                for (i = 1; i < n; i++) m[i] = (dE[i] * Lb[i]) /
                    (K[i] * (K[i] + 2 * mass));
                math_pchip_initialise(dif, n,
                    table_get_NI_el(physics, scheme, material, 0), K, m);
        }
}

/**
 * Compute the cumulative integrals of the magnetic transport.
 *
 * @param Physics   Handle for physics tables.
 * @param imed      The index of the material to tabulate.
 *
 * Compute the cumulative integrals for the momenta of the magnetic deflection
 * using a trapezoidal rule.
 */
void compute_csda_magnetic_transport(
    struct pumas_physics * physics, int material)
{
        double x[N_LARMOR_ORDERS];
        double dx[N_LARMOR_ORDERS];
        memset(x, 0x0, sizeof(x));

        /* The magnetic phase shift is proportional to the proper time integral.
         * We refer to this table. */
        const double factor = LARMOR_FACTOR / physics->mass;
        double * const T = table_get_T(physics, PUMAS_MODE_CSDA, material, 0);

        /* Compute the deflection starting from max energy down to 0 */
        double * const X0 =
            table_get_X(physics, PUMAS_MODE_CSDA, material, 0);
        const int imax = physics->n_energies - 1;
        int i, j;
        for (i = physics->n_energies - 2; i >= 1; i--) {
                double dX0 = 0.5 * (X0[i + 1] - X0[i]);
                double p1 = (T[imax] - T[i]) * factor;
                double p2 = (T[imax] - T[i + 1]) * factor;

                double f1 = 1., f2 = 1.;
                for (j = 0; j < N_LARMOR_ORDERS; j++) {
                        *table_get_Li(physics, material, j, i + 1) = x[j];
                        dx[j] = dX0 * (f1 + f2);
                        x[j] += dx[j];
                        f1 *= p1;
                        f2 *= p2;
                }
        }

        /* Extrapolate the end points */
        for (j = 0; j < N_LARMOR_ORDERS; j++) {
                *table_get_Li(physics, material, j, 1) = x[j];
                double hx = (X0[1] - X0[0]) / (X0[2] - X0[1]);
                *table_get_Li(physics, material, j, 0) = x[j] + hx * dx[j];
        }
}

/**
 * Compute and tabulate the multiple scattering parameters.
 *
 * @param Physics  Handle for physics tables.
 * @param material The target material.
 * @param row      The kinetic energy index to compute for.
 * @param error    The error data.
 *
 * At output the *row* of `physics::table_Mu0` and `physics::table_Ms1` are
 * updated. This routine manages a temporary workspace memory buffer. Calling
 * it with a negative *material* index causes the workspace memory to be freed.
 */
enum pumas_return compute_scattering_parameters(struct pumas_physics * physics,
    int material, int row, struct error_context * error_)
{
        /* Handle the memory for the temporary workspace. */
        static struct coulomb_workspace * workspace = NULL;
        if (material < 0) {
                deallocate(workspace);
                workspace = NULL;
                return PUMAS_RETURN_SUCCESS;
        } else if (workspace == NULL) {
                const int work_size = sizeof(struct coulomb_workspace) +
                    physics->max_components * sizeof(struct coulomb_data);
                workspace = allocate(work_size);
                if (workspace == NULL) return ERROR_REGISTER_MEMORY();
        }

        /* Check the kinetic energy. */
        const double kinetic = *table_get_K(physics, row);
        if (kinetic <= 0.) {
                *table_get_Mu0(physics, material, row) = 0.;
                *table_get_Ms1(physics, PUMAS_MODE_DISABLED, material, row) = 0.;
                *table_get_Ms1(physics, PUMAS_MODE_CSDA, material, row) = 0.;
                *table_get_Ms1(physics, PUMAS_MODE_MIXED, material, row) = 0.;
                return PUMAS_RETURN_SUCCESS;
        }

        /* Compute the mean free path, the first transport cross-section and
         * the minimal screening.
         */
        double cs_m = 0., cs1_m = 0.;
        double A = DBL_MAX, cs_A = 0.;
        int i;
        struct coulomb_data * data;
        for (i = 0, data = workspace->data; i < physics->elements_in[material];
             i++, data++) {
                double G[2];
                const struct material_component * const component =
                    &physics->composition[material][i];
                const struct atomic_element * const element =
                    physics->element[component->element];
                double kinetic0;
                coulomb_frame_parameters(element->Z, element->A,
                    physics->mass, kinetic, &kinetic0, data->fCM);
                data->fspin = coulomb_spin_factor(physics->mass, kinetic);
                coulomb_screening_parameters(element->Z, element->A,
                    physics->mass, kinetic, kinetic0, &data->n_parameters,
                    data->amplitude, data->screening);
                coulomb_pole_reduction(data->n_parameters, data->amplitude,
                    data->screening, data->a, data->b, data->c);
                coulomb_transport_coefficients(1., data->fspin,
                    data->n_parameters, data->screening, data->a, data->b,
                    data->c, G);
                const double normalisation = component->fraction /
                    coulomb_normalisation(element->Z, element->A,
                        physics->mass, kinetic, kinetic0);

                cs_m += normalisation * G[0];
                data->normalisation = normalisation;
                const double d = 1. / (data->fCM[0] * (1. + data->fCM[1]));
                cs1_m += normalisation * G[1] * d * d;

                int j;
                for (j = 0; j < data->n_parameters - 1; j++) {
                        const double Aj = data->screening[j];
                        if (Aj < A) A  = Aj;
                }
                cs_A += normalisation;
        }
        cs_A /= A * (A + 1.);

        /* Set the hard scattering mean free path. */
        const double range_i = 1. / *table_get_X(
            physics, PUMAS_MODE_CSDA, material, row);
        double cs_h = ((cs1_m > range_i) ? cs1_m : range_i) /
            physics->elastic_ratio;
        if (cs_h < 1. / EHS_PATH_MAX) cs_h = 1. / EHS_PATH_MAX;

        /* Compute the hard scattering cutoff angle, in the CM. */
        const double max_mu0 = 0.5 * (1. - cos(MAX_SOFT_ANGLE * M_PI / 180.));
        double mu0 = 0., lb_h;
        if (cs_h < cs_m) {
                /* Compute an upper bound on mu0 using a bounding Wentzel
                 * cross-section.
                 */
                const double zeta = cs_h / cs_A;
                double mu_max = A * (1. - zeta) / (A + zeta);
                if (mu_max > max_mu0) mu_max = max_mu0;

                /* Configure for the root solver. */
                workspace->cs_h = cs_h;
                workspace->material = material;
                workspace->ihard = -1;

                const double mu_min = 0;
                const double fmax =
                    compute_cutoff_objective(physics, mu_max, workspace);
                const double fmin = cs_m - cs_h;
                double mubest;
                if (math_find_root(compute_cutoff_objective, physics, mu_min,
                    mu_max, &fmin, &fmax, 1E-06 * mu_max,
                    1E-06, 100, workspace, &mubest) == 0) {
                        mu0 = mubest;
                } else {
                        mu0 = mu_max;
                }
                if (mu0 > max_mu0) mu0 = max_mu0;
                cs_h += compute_cutoff_objective(physics, mu0, workspace);
                if (cs_h <= 1. / EHS_PATH_MAX)
                        lb_h = EHS_PATH_MAX;
                else
                        lb_h = 1. / cs_h;
        } else
                lb_h = 1. / cs_m;

        *table_get_Mu0(physics, material, row) = mu0;
        *table_get_Lb(physics, material, row) =
            lb_h * kinetic * (kinetic + 2. * physics->mass);
        *table_get_NI_el(physics, PUMAS_MODE_CSDA, material, row) = 1. /
            (*table_get_dE(physics, PUMAS_MODE_CSDA, material, row) * lb_h);
        *table_get_NI_el(physics, PUMAS_MODE_MIXED, material, row) = 1. /
            (*table_get_dE(physics, PUMAS_MODE_MIXED, material, row) * lb_h);

        /* Compute the 1st moment of the soft scattering. */
        const int n0 = physics->n_materials - physics->n_composites;
        if (material < n0) {
                /* We have a base material. */
                static double * ms1_table = NULL;
                if (material == 0) {
                        /* Precompute the per element soft scattering terms. */
                        enum pumas_return rc;
                        if ((rc = compute_msc_soft(physics, row, &ms1_table,
                                 error_)) != PUMAS_RETURN_SUCCESS)
                                return rc;
                }

                double invlb1 = 0., invlb1_csda = 0., invlb1_hybrid = 0.;
                struct material_component * component =
                    physics->composition[material];
                for (i = 0, data = workspace->data;
                     i < physics->elements_in[material];
                     i++, data++, component++) {
                        /* Elastic contribution to the transport. */
                        double G[2];
                        coulomb_transport_coefficients(mu0, data->fspin,
                            data->n_parameters, data->screening, data->a,
                            data->b, data->c, G);
                        double d = 1. / (data->fCM[0] * (1. + data->fCM[1]));
                        d *= d;
                        invlb1 += data->normalisation * d * G[1];

                        /* Other precomputed soft scattering terms. */
                        const int iel = component->element;
                        invlb1_csda += *table_get_ms1(physics, PUMAS_MODE_CSDA,
                            iel, row, ms1_table) * component->fraction;
                        invlb1_hybrid += *table_get_ms1(physics,
                            PUMAS_MODE_MIXED, iel, row, ms1_table) *
                            component->fraction;
                }

                invlb1_csda += compute_msc_electronic(
                    physics, PUMAS_MODE_CSDA, material, row);
                invlb1_hybrid += compute_msc_electronic(
                    physics, PUMAS_MODE_MIXED, material, row);

                *table_get_Ms1(physics, PUMAS_MODE_DISABLED, material, row) =
                    invlb1;
                *table_get_Ms1(physics, PUMAS_MODE_CSDA, material, row) =
                    invlb1 + invlb1_csda;
                *table_get_Ms1(physics, PUMAS_MODE_MIXED, material, row) =
                    invlb1 + invlb1_hybrid;
        } else {
                /* We have a composite material. First we loop on atomic
                 * elements and compute the elastic scattering contribution.
                 */
                double invlb1 = 0.;
                struct material_component * component =
                    physics->composition[material];
                for (i = 0, data = workspace->data;
                     i < physics->elements_in[material];
                     i++, data++, component++) {
                        /* Elastic contribution to the transport. */
                        double G[2];
                        coulomb_transport_coefficients(mu0, data->fspin,
                            data->n_parameters, data->screening, data->a,
                            data->b, data->c, G);
                        double d = 1. / (data->fCM[0] * (1. + data->fCM[1]));
                        d *= d;
                        invlb1 += data->normalisation * d * G[1];
                }

                /* Then, let's loop on the base material components and add
                 * contributions from other processes.
                 */
                double invlb1_csda = 0., invlb1_hybrid = 0.;
                const struct composite_material * composite =
                    physics->composite[material - n0];
                int icomp;
                for (icomp = 0; icomp < composite->n_components; icomp++) {
                        const struct composite_component * c =
                            composite->component + icomp;
                        const int imat = c->material;

                        /* Base material soft scattering. */
                        const double tmp = *table_get_Ms1(
                            physics, PUMAS_MODE_DISABLED, imat, row);
                        invlb1_csda += (*table_get_Ms1(
                            physics, PUMAS_MODE_CSDA, imat, row) - tmp) *
                            c->fraction;
                        invlb1_hybrid += (*table_get_Ms1(
                            physics, PUMAS_MODE_MIXED, imat, row) - tmp) *
                            c->fraction;
                }
                *table_get_Ms1(physics, PUMAS_MODE_DISABLED, material, row) =
                    invlb1;
                *table_get_Ms1(physics, PUMAS_MODE_CSDA, material, row) =
                    invlb1 + invlb1_csda;
                *table_get_Ms1(physics, PUMAS_MODE_MIXED, material, row) =
                    invlb1 + invlb1_hybrid;
        }

        return PUMAS_RETURN_SUCCESS;
}

/**
 * Tabulate the multiple scattering per element.
 *
 * @param Physics   Handle for physics tables.
 * @param row       The row index for the kinetic value.
 * @param data      The tabulated data.
 * @param error_    The error data.
 * @return On success `PUMAS_RETRUN_SUCCESS` otherwise an error code.
 *
 * Because this step is time consuming, the multiple scattering 1st path
 * lengths are precomputed per element at initialisation using a temporary
 * buffer. These temporary tables are used for computing per material 1st path
 * length.
 *
 * **Note** This routine handles a static dynamically allocated table. If the
 * *row* index is negative the table is freed.
 */
enum pumas_return compute_msc_soft(struct pumas_physics * physics, int row,
    double ** data, struct error_context * error_)
{
        static double * ms1_table = NULL;
        if (data != NULL) *data = NULL;

        if (row < 0) {
                deallocate(ms1_table);
                ms1_table = NULL;
                return PUMAS_RETURN_SUCCESS;
        }

        /* Allocate the temporary table. */
        if (ms1_table == NULL) {
                ms1_table = allocate(N_SCHEMES * physics->n_elements *
                    physics->n_energies * sizeof(double));
                if (ms1_table == NULL) return ERROR_REGISTER_MEMORY();
        }

        /* Loop over atomic elements. */
        const double kinetic = *table_get_K(physics, row);
        int iel;
        for (iel = 0; iel < physics->n_elements; iel++) {
                struct atomic_element * element = physics->element[iel];
                double invlb1_csda = 0., invlb1_hybrid = 0.;

                /* Radiative contributions to the transverse transport. */
                invlb1_csda += dcs_bremsstrahlung_transport(
                    physics, element, kinetic, 1.);
                invlb1_hybrid += dcs_bremsstrahlung_transport(
                    physics, element, kinetic, physics->cutoff);
                invlb1_csda += dcs_pair_production_transport(
                    physics, element, kinetic, 1.);
                invlb1_hybrid += dcs_pair_production_transport(
                    physics, element, kinetic, physics->cutoff);
                invlb1_csda += dcs_photonuclear_transport(
                    physics, element, kinetic, 1.);
                invlb1_hybrid += dcs_photonuclear_transport(
                    physics, element, kinetic, physics->cutoff);

                *table_get_ms1(
                    physics, PUMAS_MODE_CSDA, iel, row, ms1_table) =
                        invlb1_csda;
                *table_get_ms1(
                    physics, PUMAS_MODE_MIXED, iel, row, ms1_table) =
                        invlb1_hybrid;
        }

        *data = ms1_table;
        return PUMAS_RETURN_SUCCESS;
}

/**
 * Tabulate the electronic multiple scattering for a material.
 *
 * @param Physics   Handle for physics tables.
 * @param mode      The energy loss mode.
 * @param material  The material index.
 * @param row       The row index for the kinetic value.
 * @return The computed value.
 */
double compute_msc_electronic(struct pumas_physics * physics,
    enum pumas_mode mode, int material, int row)
{
        const double kinetic = *table_get_K(physics, row);
        const double nu = (mode == PUMAS_MODE_CSDA) ?
            kinetic : physics->cutoff * kinetic;
        return transverse_transport_electronic(
            physics->material_ZoA[material], physics->material_I[material],
            physics->material_aS[material], physics->mass, kinetic, nu);
}

/**
 * Objective function for solving the soft scattering cut-off angle.
 *
 * @param Physics    Handle for physics tables.
 * @param mu         The proposed cutoff angular parameter.
 * @param parameters A pointer to the temporary workspace.
 * @return The difference between the current and expected restricted cross
 * section.
 *
 * This is a wrapper for the root solver. It provides the objective function
 * to solve for.
 */
double compute_cutoff_objective(
    const struct pumas_physics * physics, double mu, void * parameters)
{
        /* Unpack the workspace. */
        struct coulomb_workspace * workspace = parameters;

        /* Compute the restricted cross section. */
        double cs_tot = 0.;
        int i, n = physics->elements_in[workspace->material];
        struct coulomb_data * data;
        for (i = 0, data = workspace->data; i < n; i++, data++) {
                cs_tot +=
                    data->normalisation * coulomb_restricted_cs(mu, data->fspin,
                                              data->n_parameters,
                                              data->screening, data->a,
                                              data->b, data->c);
        }

        /* Return the difference with the expectation. */
        return cs_tot - workspace->cs_h;
}

/**
 * Tabulate the deterministic CEL and DEL cross-sections per element.
 *
 * @param Physics   Handle for physics tables.
 * @param row       The row index for the kinetic value.
 * @return          The tabulated data.
 *
 * Because this step is time consuming, the CEL integration is precomputed per
 * element at initialisation using a temporary buffer. These temporary tables
 * are used for computing per material CEL corrections and DEL cross-sections.
 *
 * **Note** This routine handles a static dynamically allocated table. If the
 * *row* index is negative the table is freed.
 */
double * compute_cel_and_del(struct pumas_physics * physics, int row)
{
        static double * cel_table = NULL;

        if (row < 0) {
                deallocate(cel_table);
                return (cel_table = NULL);
        }

        /* Allocate the temporary table. */
        if (cel_table == NULL) {
                cel_table = allocate(2 * N_DEL_PROCESSES * physics->n_elements *
                    physics->n_energies * sizeof(double));
                if (cel_table == NULL) return NULL;
        }

        /* Loop over atomic elements. */
        const double kinetic = *table_get_K(physics, row);
        int iel;
        for (iel = 0; iel < physics->n_elements; iel++) {
                const struct atomic_element * element = physics->element[iel];

                /* Loop over processes. */
                int ip;
                for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                        dcs_function_t * dcs = dcs_get(ip);
                        *table_get_CSn(physics, ip, iel, row) =
                            compute_dcs_integral(physics, 0, element, kinetic,
                                dcs, physics->cutoff, 1, 180);
                        *table_get_cel(physics, ip, iel, row, cel_table) =
                            compute_dcs_integral(physics, 1, element, kinetic,
                                dcs, physics->cutoff, 1, 180);
                        const double stg = (dcs == dcs_ionisation) ?
                            compute_dcs_integral(physics, 2, element, kinetic,
                                dcs, 0, physics->cutoff, 180) : 0.;
                        *table_get_stg(physics, ip, iel, row, cel_table) = stg;
                }
        }

        return cel_table;
}

/**
 * Regularise the cross-section table and related quantities with a
 * *do nothing* process.
 *
 * @param material The index of the propagation material.
 */
void compute_regularise_del(struct pumas_physics * physics, int material)
{
        /* Find the kinetic threshold above which the total cross-section is
         * not null. */
        int it;
        double cs0;
        for (it = 1; it < physics->n_energies; it++)
                if ((cs0 = *table_get_CS(physics, material, it)) != 0) break;
        *table_get_Kt(physics, material) = *table_get_K(physics, it);

        /*
         * Regularise the cross section and the total number of interaction
         * lengths.
         */
        int row;
        for (row = 1; row < it; row++) {
                *table_get_CS(physics, material, row) = cs0;
                const double dEdX =
                    *table_get_dE(physics, PUMAS_MODE_MIXED, material, row);
                *table_get_NI_in(physics, material, row) = cs0 / dEdX;
        }

        if (material > 0)
                return; /* The computations below need to be done only
once. */

        /* Find the kinetic threshold per process and atomic element. */
        int iel, ip;
        for (iel = 0; iel < physics->n_elements; iel++) {
                const struct atomic_element * element = physics->element[iel];
                for (ip = 0; ip < N_DEL_PROCESSES; ip++) {
                        for (row = 0; row < it; row++)
                                *table_get_Xt(physics, ip, iel, row) = 1.;
                        dcs_function_t * dcs_func = dcs_get(ip);
                        for (row = it; row < physics->n_energies; row++) {
                                const double k = *table_get_K(physics, row);
                                double x = physics->cutoff;
                                while ((x < 1.) && (dcs_func(physics, element,
                                                        k, k * x) <= 0.))
                                        x *= 2;
                                if (x >= 1.)
                                        x = 1.;
                                else if (x > physics->cutoff) {
                                        const double eps = 1E-02 *
                                            physics->cutoff;
                                        double x0 = 0.5 * x;
                                        double dcs = 0.;
                                        for (;;) {
                                                if (dcs == 0.)
                                                        x0 += 0.5 * (x - x0);
                                                else {
                                                        const double dx =
                                                            x - x0;
                                                        x = x0;
                                                        x0 -= 0.5 * dx;
                                                }
                                                if ((x - x0) <= eps) break;
                                                dcs = dcs_func(physics, element,
                                                    k, k * x0);
                                        }
                                }
                                *table_get_Xt(physics, ip, iel, row) = x;
                        }
                }
        }
}

/**
 * Compute integrals of DCSs.
 *
 * @param Physics Handle for physics tables.
 * @param mode    Flag to select the integration mode.
 * @param element The target atomic element.
 * @param kinetic The initial or final kinetic energy.
 * @param dcs     Handle to the dcs function.
 * @param xlow    The lower bound of the fractional energy transfer.
 * @param xlow    The upper bound of the fractional energy transfer.
 * @param nint    The requested number of point for the integral.
 * @return The integrated dcs, in m^2/kg or the energy loss in GeV m^2/kg.
 *
 * The parameter *mode* controls the integration mode as following. If *mode* is
 * `0` the restricted cross section is computed, Else, if mode is `1` the
 * restricted energy loss is computed, else the restricted energy straggling is
 * computed.
 */
double compute_dcs_integral(struct pumas_physics * physics, int mode,
    const struct atomic_element * element, double kinetic, dcs_function_t * dcs,
    double xlow, double xhigh, int nint)
{
        if (xlow <= 0) xlow = 1E-06; /* Values below do not impact the
                                      * integral values.
                                      */

        /* Let us use the analytical form for ionisation */
        if (dcs == &dcs_ionisation) {
                return dcs_ionisation_integrate(
                    physics, mode, element, kinetic, xlow, xhigh);
        }

        /* Set the integration boundaries */
        double qmin = 0, qmax;
        if (dcs == &dcs_photonuclear) {
                const double M = 0.5 * (NEUTRON_MASS + PROTON_MASS);
                const double mpi = PION_MASS;
                qmin = mpi + 0.5 * mpi * mpi / M;
                qmax = kinetic + physics->mass -
                    0.5 * (M + physics->mass * physics->mass / M);
        } else {
                const double Z13 = pow(element->Z, 1. / 3.);
                const double sqrte = 1.648721271;
                qmax = kinetic + physics->mass * (1. - 0.75 * sqrte * Z13);

                if (dcs == &dcs_pair_production) {
                        qmin = 4 * ELECTRON_MASS;
                }
        }

        double qlow = xlow * kinetic;
        if (qlow < qmin) qlow = qmin;
        double qhigh = xhigh * kinetic;
        if (qhigh > qmax) qhigh = qmax;
        if (qlow >= qhigh) return 0.;

        /* We integrate over the recoil energy using a logarithmic sampling. */
        double dcsint = 0.;
        double x0 = log(qlow), x1 = log(qhigh);
        math_gauss_quad(nint, &x0, &x1); /* Initialisation. */

        double xi, wi;
        while (math_gauss_quad(0, &xi, &wi) == 0) { /* Iterations. */
                const double qi = exp(xi);
                double y = dcs(physics, element, kinetic, qi) * qi;
                if (mode > 0) y *= qi;
                if (mode > 1) y *= qi;
                dcsint += y * wi;
        }
        dcsint /= kinetic + physics->mass;

        return dcsint;
}

/**
 * Compute or update the relative electron density of a material.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 */
void compute_ZoA(struct pumas_physics * physics, int material)
{
        int i;
        double ZoA = 0.;
        for (i = 0; i < physics->elements_in[material]; i++) {

                const struct material_component * const c =
                    physics->composition[material] + i;
                const struct atomic_element * const e =
                    physics->element[c->element];
                ZoA += e->Z / e->A * c->fraction;
        }
        physics->material_ZoA[material] = ZoA;
}

/**
 * Compute or update the Mean Excitation Energy of a material.
 *
 * @param Physics  Handle for physics tables.
 * @param material The material index.
 */
void compute_MEE(struct pumas_physics * physics, int material)
{
        double lnI = 0., Z = 0.;
        struct material_component * component;
        int iel;
        for (iel = 0, component = physics->composition[material];
             iel < physics->elements_in[material]; iel++, component++) {
                struct atomic_element * e =
                    physics->element[component->element];
                const double nZ = component->fraction * e->Z / e->A;
                lnI += nZ * log(e->I);
                Z += nZ;
        }
        physics->material_I[material] = exp(lnI / Z);
}

/*
 * Low level routines: Models for the differential cross-sections and helper
 * functions.
 *
 * Note that the macroscopic DCS per fractional energy loss and per mass unit
 * are actualy used, i.e. dSigma/dnu. The unit is an inverse column density,
 * expressed in m^2/kg. The corresponding interaction length is
 * `Lint = 1/(Sigma*rho)` with rho the medium density.
 */
/**
 * Get a differential cross-section given a process index.
 *
 * @param process The process index.
 * @return The differential cross section function.
 *
 * `index = 0` corresponds to Bremsstrahlung, `index = 1` to Pair Production,
 * `index = 2` to Photonuclear interactions and `index = 3` to Ionisation.
 */
dcs_function_t * dcs_get(int process)
{
        dcs_function_t * const dcs_func[] = { dcs_bremsstrahlung,
                dcs_pair_production, dcs_photonuclear, dcs_ionisation };
        return dcs_func[process];
}

/**
 * Get the table index for a given DCS function.
 *
 * @param dcs_function The DCS function.
 * @return The corresponding table index.
 *
 * This is the inverse mapping than dcs_get.
 */
int dcs_get_index(dcs_function_t * dcs_func)
{
        if (dcs_func == dcs_bremsstrahlung)
                return 0;
        else if (dcs_func == dcs_pair_production)
                return 1;
        else if (dcs_func == dcs_photonuclear)
                return 2;
        return 3;
}

/**
 * Get the kinematic range for a given process.
 */
void dcs_get_range(int process, double Z, double mass,
    double energy, double * qmin, double * qmax)
{
        if (process == 2) {
                dcs_photonuclear_range(mass, energy, qmin, qmax);
        } else {
                dcs_pair_production_range(Z, mass, energy, qmin, qmax);
                if (process == 0) *qmin = 0.;
        }
}

/**
 * Wrapper for the Bremsstrahlung differential cross section.
 *
 * @param Physics Handle for physics tables.
 * @param element The target atomic element.
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The differential cross section in m^2/kg.
 */
double dcs_bremsstrahlung(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q)
{
        return physics->dcs_bremsstrahlung(element->Z, element->A,
            physics->mass, K, q) * 1E+03 * AVOGADRO_NUMBER *
            (physics->mass + K) / element->A;
}

/**
 * Wrapper for the pair production differential cross section.
 *
 * @param Physics Handle for physics tables.
 * @param element The target atomic element.
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The differential cross section in m^2/kg.
 */
double dcs_pair_production(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q)
{
        return physics->dcs_pair_production(element->Z, element->A,
            physics->mass, K, q) * 1E+03 * AVOGADRO_NUMBER *
            (physics->mass + K) / element->A;
}

/**
 * Wrapper for the photonuclear differential cross section.
 *
 * @param Physics Handle for physics tables.
 * @param element The target atomic element.
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The differential cross section in m^2/kg.
 */
double dcs_photonuclear(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q)
{
        if (dcs_photonuclear_check(physics->mass, K, q)) {
                /* Check the kinematic range. */
                return 0.;
        } else {
                return physics->dcs_photonuclear(element->Z, element->A,
                    physics->mass, K, q) * 1E+03 * AVOGADRO_NUMBER *
                    (physics->mass + K) / element->A;
        }
}

/**
 * Utility function for getting the kinematic range for the photonuclear process
 *
 * @param m    The projectile rest mass.
 * @param K    The projectile kinetic energy.
 * @param qmin The min kinetic energy lost to the photon.
 * @param qmax The max kinetic energy lost to the photon.
 */
void dcs_photonuclear_range(double m, double K, double * qmin, double * qmax)
{
        const double M = 0.5 * (NEUTRON_MASS + PROTON_MASS);
        const double mpi = PION_MASS;
        if (qmin != NULL) *qmin = mpi + 0.5 * mpi * mpi / M;
        if (qmax != NULL) *qmax = K + m - 0.5 * (M + m * m / M);
}

/**
 * Utility function for checking the kinematic range of the Photonuclear model.
 *
 * @param m The projectile rest mass.
 * @param K The projectile kinetic energy.
 * @param q The kinetic energy lost to the photon.
 * @return `0` if the model is within kinematic range.
 */
int dcs_photonuclear_check(double m, double K, double q)
{
        double qmin, qmax;
        dcs_photonuclear_range(m, K, &qmin, &qmax);
        return (q < qmin) || (q > qmax);
}

/* API function for the effective electronic DCS */
double pumas_electronic_dcs(double Z, double I, double m, double K, double q)
{
        const double P2 = K * (K + 2. * m);
        const double E = K + m;
        const double Wmax = 2. * ELECTRON_MASS * P2 /
            (m * m + ELECTRON_MASS * (ELECTRON_MASS + 2. * E));
        if (q > Wmax) return 0.;
        const double Wmin = (13.6 / 19.2) * I;
        if (q <= Wmin) return 0.;

        /* Close interactions for Q >> atomic binding energies. */
        const double E2 = E * E;
        const double beta2 = P2 / E2;
        const double a0 = 0.5 / E2;
        const double a1 = beta2 / Wmax;
        const double cs = 2 * M_PI * ELECTRON_MASS * ELECTRON_RADIUS *
            ELECTRON_RADIUS * Z * (a0 + 1. / q * (1 / q - a1)) / beta2;

        return cs;
}

/**
 * The ionisation differential cross section.
 *
 * @param Physics Handle for physics tables.
 * @param element The target atomic element.
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The differential cross section in m^2/kg
 *
 * The differential cross section for ionisation is computed following
 * Salvat NIMB316 (2013) 144-159, considering only close interactions
 * for DELs. In addition a radiative correction is applied according to
 * Sokalski et al., Phys.Rev.D64 (2001) 074015 (MUM).
 */
double dcs_ionisation(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double q)
{
        return pumas_electronic_dcs(element->Z, element->I, physics->mass, K,
            q) * (physics->mass + K) * AVOGADRO_NUMBER / (element->A * 1E-03);
}

/**
 * The analytical form for the partial integral of the ionisation DCS.
 *
 * @param Physics Handle for physics tables.
 * @param mode    Flag to select the integration mode.
 * @param element The target atomic element.
 * @param K       The projectile initial kinetic energy.
 * @param xlow    The lower bound of the fractional energy transfer.
 * @param xlow    The upper bound of the fractional energy transfer.
 * @return The integrated dcs, in m^2/kg or the energy loss in GeV m^2/kg.
 *
 * The parameter *mode* controls the integration mode as following. If *mode*
 * is `0` the restricted cross section is computed.  Else, if mode is `1` then
 * the restricted energy loss is computed, else the restricted energy straggling
 * is computed.
 */
double dcs_ionisation_integrate(const struct pumas_physics * physics, int mode,
    const struct atomic_element * element, double K, double xlow, double xhigh)
{
        const double Z = element->Z;
        const double A = element->A;
        const double P2 = K * (K + 2. * physics->mass);
        const double E = K + physics->mass;
        const double Wr = 2. * ELECTRON_MASS * P2 /
            (physics->mass * physics->mass +
                                ELECTRON_MASS * (ELECTRON_MASS + 2. * E));
        const double qlow = K * xlow;
        const double qhigh = K * xhigh;
        double Wmax = Wr;
        if (Wmax < qlow) return 0.;
        if (Wmax > qhigh) Wmax = qhigh;
        double Wmin = (13.6 / 19.2) * element->I;
        if (qlow >= Wmin) Wmin = qlow;

        /* Check the bounds. */
        if (Wmax <= Wmin) return 0.;

        /* Close interactions for Q >> atomic binding energies. */
        const double E2 = E * E;
        const double beta2 = P2 / E2;
        const double a0 = 0.5 / E2;
        const double a1 = beta2 / Wr;

        double I;
        if (mode == 0) {
                I = a0 * (Wmax - Wmin) - a1 * log(Wmax / Wmin) +
                    1. / Wmin - 1. / Wmax;
        } else if (mode == 1) {
                I = 0.5 * a0 * (Wmax * Wmax - Wmin * Wmin) -
                    a1 * (Wmax - Wmin) + log(Wmax / Wmin);
        } else {
                const double Wmax2 = Wmax * Wmax;
                const double Wmin2 = Wmin * Wmin;
                I = a0 * (Wmax2 * Wmax - Wmin2 * Wmin) / 3. -
                    0.5 * a1 * (Wmax2 - Wmin2) + Wmax - Wmin;
        }

        return 2 * M_PI * ELECTRON_MASS * ELECTRON_RADIUS * ELECTRON_RADIUS *
            Z * AVOGADRO_NUMBER / (A * 1E-03 * beta2) * I;
}

double dcs_ionisation_randomise(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double K, double xlow)
{
        const double P2 = K * (K + 2. * physics->mass);
        const double E = K + physics->mass;
        const double Wmax = 2. * ELECTRON_MASS * P2 /
            (physics->mass * physics->mass +
                                ELECTRON_MASS * (ELECTRON_MASS + 2. * E));
        if (Wmax < physics->cutoff * K) return K;
        double Wmin = (13.6 / 19.2) * element->I;
        const double qlow = K * xlow;
        if (qlow >= Wmin) Wmin = qlow;

        /* Close interactions for Q >> atomic binding energies. */
        const double E2 = E * E;
        const double beta2 = P2 / E2;
        const double a0 = 0.5 / E2;
        const double a1 = beta2 / Wmax;

        const double p0 = a0 * (Wmax - Wmin);
        const double p2 = 1. / Wmin - 1. / Wmax;
        double q;
        for (;;) {
                /* Inverse sampling using an enveloppe as a sum of two PDFs. */
                double z = context->random(context) * (p0 + p2);
                if (z <= p0) {
                        z /= p0;
                        q = (Wmax - Wmin) * z;
                } else {
                        z = (z - p0) / p2;
                        q = Wmin * Wmax / (Wmax - z * (Wmax - Wmin));
                }

                /* Rejection sampling. */
                const double r0 = a0 + 1 / (q * q);
                const double r1 = r0 - a1 / q;
                if (context->random(context) * r0 <= r1) break;
        }

        return K - q;
}

/**
 * Encapsulation for the evaluation of DCS.
 *
 * @param Physics  Handle for physics tables.
 * @param context  The simulation context.
 * @param dcs_func The DCS function to evaluate.
 * @param element  The target atomic element.
 * @param K        The projectile initial kinetic energy.
 * @param q        The transfered energy.
 * @return The DCS value or `0`.
 *
 * This routine encapsulate the evaluation of DCS during the MC. It takes care
 * of checking whether an approximate model can be used or not. In addition it
 * applies a Jacobian weight factor for changing from nu = q / E to x = q / K.
 */
double dcs_evaluate(const struct pumas_physics * physics,
    struct pumas_context * context, dcs_function_t * dcs_func,
    const struct atomic_element * element, double K, double q)
{
        /* Compute the Jacobian factor. */
        const double wj = K / (K + physics->mass);

        /* Check if the process has a valid tabulated model. */
        if (dcs_func == dcs_ionisation)
                return dcs_func(physics, element, K, q) * wj;

        /* Check the kinematic range */
        double qlow = 0;
        if (dcs_func == dcs_photonuclear) {
                double qmin, qmax;
                dcs_photonuclear_range(physics->mass, K, &qmin, &qmax);
                if ((q < qmin) || (q > qmax)) return 0.;
                else qlow = 2 * qmin;
        } else {
                double qmin, qmax;
                dcs_pair_production_range(
                    element->Z, physics->mass, K, &qmin, &qmax);
                if (dcs_func == dcs_pair_production) qlow = 4 * qmin;
                else qmin = 0.;
                if ((q < qmin) || (q > qmax)) return 0.;
        }
        if (q < qlow) {
                return dcs_func(physics, element, K, q) * wj;
        }

        /* Check if the exact computation should be used and prepare spline
         * coefficients.
         */
        const int nx = physics->n_table_dcs;
        const float xmin = physics->table_DCS_x[0];
        const float xmax = physics->table_DCS_x[nx - 1];
        const float x = logf((float)(q / K));
        if ((x < xmin) || (x >= xmax)) {
                return dcs_func(physics, element, K, q) * wj;
        }
        int ix = 0, tmpi = nx - 1;
        table_bracketf(physics->table_DCS_x, x, &ix, &tmpi);
        if ((ix < 0) || (ix >= nx - 1)) {
                return dcs_func(physics, element, K, q) * wj;
        }

        float p00, p01, m00, m01, p10, p11, m10, m11, h0, h1;
        const int ip = dcs_get_index(dcs_func);
        const int imax = physics->n_energies - 1;
        const double Kmax = *table_get_K(physics, imax);
        int i0;
        if ((K >= Kmax) || ((i0 = table_index(physics, context,
                                 table_get_K(physics, 0), K)) >= imax)) {
                /* Use the last tabulated value. */
                const float * data = table_get_dcs(
                    physics, ip, element->index, imax);
                p00 = p10 = data[2 * ix];
                m00 = m10 = data[2 * ix + 1];
                p01 = p11 = data[2 * ix + 2];
                m01 = m11 = data[2 * ix + 3];
                h0 = 1.f;
                h1 = 0.f;
        } else {
                const float * data =
                    table_get_dcs(physics, ip, element->index, i0);
                p00 = data[2 * ix];
                m00 = data[2 * ix + 1];
                p01 = data[2 * ix + 2];
                m01 = data[2 * ix + 3];
                data += 2 * nx;
                p10 = data[2 * ix];
                m10 = data[2 * ix + 1];
                p11 = data[2 * ix + 2];
                m11 = data[2 * ix + 3];

                const float K0 = (float)(*table_get_K(physics, i0));
                const float K1 = (float)(*table_get_K(physics, i0 + 1));
                h1 = logf(((float)K) / K0) / logf(K1 / K0);
                h0 = 1.f - h1;
        }

        if (((p00 == 0.f) && (m00 == 0.f)) || ((p01 == 0.f) && (m01 == 0.f)) ||
            ((p10 == 0.f) && (m10 == 0.f)) || ((p11 == 0.f) && (m11 == 0.f))) {
                return dcs_func(physics, element, K, q) * wj;
        }

        const float x0 = physics->table_DCS_x[ix];
        const float x1 = physics->table_DCS_x[ix + 1];
        const float dx = x1 - x0;
        m00 *= dx;
        m01 *= dx;
        const float t = (x - x0) / dx;
        const float c02 = -3 * (p00 - p01) - 2 * m00 - m01;
        const float c03 = 2 * (p00 - p01) + m00 + m01;
        float r = expf(p00 + t * (m00 + t * (c02 + t * c03))) * h0;
        if (h1 > 0.f) {
                m10 *= dx;
                m11 *= dx;
                const float c12 = -3 * (p10 - p11) - 2 * m10 - m11;
                const float c13 = 2 * (p10 - p11) + m10 + m11;
                r += expf(p10 + t * (m10 + t * (c12 + t * c13))) * h1;
        }

        return r * wj;
}

struct dcs_tabulate_work {
        int imin;
        int imax;
        double * x;
        double * y;
        double * m;
        double data[];
};

static void dcs_tabulate_row(
    struct pumas_physics * physics, int process, int element, int row,
    struct dcs_tabulate_work * work)
{
        const int n = physics->n_table_dcs;
        const double cs = *table_get_CSn(physics, process, element, row);
        int i;
        if (cs <= 0.) {
                float * data = table_get_dcs(physics, process, element, row);
                for (i = 0; i < 2 * n; i++) {
                        data[i] = 0.f;
                }
                return;
        }

        const double K = physics->table_K[row];
        dcs_function_t * dcs = dcs_get(process);
        struct atomic_element * e = physics->element[element];
        work->imin = n;
        work->imax = n - 1;
        for (i = 0; i < n; i++) {
                const double xi = work->x[i];
                const double q = K * exp(xi);
                const double d = dcs(physics, e, K, q);
                if (d > 0) {
                        if (i < work->imin) work->imin = i;
                        work->y[i] = log(d);
                } else if (work->imin < n) {
                        work->imax = i - 1;
                        break;
                }
        }

        if (work->imin <= work->imax) {
                /* Cubic interpolation */
                math_pchip_initialise(
                    0, work->imax - work->imin + 1, work->x + work->imin,
                    work->y + work->imin, work->m + work->imin);
        }

        /* Kinematic filter for the envelope */
        double qmin, qmax;
        if (process == 2) {
                dcs_photonuclear_range(physics->mass, K, &qmin, &qmax);
                qmin += qmin;
        } else {
                dcs_pair_production_range(e->Z, physics->mass, K, &qmin, &qmax);
                if (process == 0) {
                        qmin = 0.5 * physics->cutoff * K;
                } else {
                        qmin *= 4;
                }
        }
        const double qrev = DCS_MODEL_X_REVERSE * K;
        qmax *= 0.5;
        if (qmax > qrev) qmax = qrev;
        const double xmin = log(qmin / K), xmax = log(qmax / K);
        int ia0 = work->imax, ia1 = work->imin;

        /* Set the DCS table and the envelope range */
        float * data = table_get_dcs(physics, process, element, row);
        for (i = 0; i < n; i++) {
                if ((i >= work->imin) && (i <= work->imax)) {
                        data[2 * i] = (float)work->y[i];
                        data[2 * i + 1] = (float)work->m[i];
                        if ((work->x[i] > xmin) && (work->x[i] < xmax)) {
                                if (ia0 > i) ia0 = i;
                                if (ia1 < i) ia1 = i;
                        }
                } else {
                        data[2 * i] = 0.f;
                        data[2 * i + 1] = 0.f;
                }
        }

        /* Set the envelope exponent */
        float * env = table_get_dcs_envelope(physics, process, element, row);
        if ((ia0 < ia1) && (ia0 < n - 1)) {
                const int ia = (3 * ia0 + ia1) / 4;
                double alpha = -work->m[ia];
                if (fabs(alpha - 1.) <= 0.1) alpha = 1.;
                env[0] = (float)alpha;
        } else {
                env[0] = 0.f;
        }
}

struct dcs_tabulate_envelope {
        dcs_function_t * dcs;
        const struct atomic_element * element;
        double K;
        double alpha;
};

static double dcs_tabulate_envelope_objective(
    const struct pumas_physics * physics, double lnx, void * params)
{
        struct dcs_tabulate_envelope * p = params;
        const double x = exp(lnx);
        const double q = x * p->K;
        return -dcs_evaluate(
            physics, NULL, p->dcs, p->element, p->K, q) * pow(x, p->alpha);
}

static void dcs_tabulate_envelope_row(
    struct pumas_physics * physics, int process, int element, int row)
{
#define RESOLUTION 1E-10

        /* Unpack data */
        float * envelope = table_get_dcs_envelope(
            physics, process, element, row);
        const double cs = *table_get_CSn(physics, process, element, row);
        if (cs <= 0.) {
                envelope[1] = 0.f;
                return;
        }
        const double K = physics->table_K[row];
        const struct atomic_element * e = physics->element[element];

        /* Get the kinematic range */
        double qmin, qmax;
        dcs_get_range(process, e->Z, physics->mass, K, &qmin, &qmax);
        double xmin = qmin / K;
        if (xmin < physics->cutoff) xmin = physics->cutoff;
        xmin = log(xmin);
        double xmax = log(qmax / K);
        if (xmin >= xmax) {
                envelope[1] = 0.f;
                return;
        }

        /* Set arguments for the objective */
        struct dcs_tabulate_envelope args = {
                .dcs = dcs_get(process),
                .element = e,
                .K = K,
                .alpha = (double)envelope[0]
        };

        /* Ensure that the DCS is strictly positive at bounds */
        double f0 = dcs_tabulate_envelope_objective(physics, xmin, &args);
        double f1 = dcs_tabulate_envelope_objective(physics, xmax, &args);
        if (f0 == 0.) {
                double x0 = xmin, x1 = xmax;
                while (x1 - x0 > RESOLUTION) {
                        const double x2 = 0.5 * (x0 + x1);
                        const double f2 = dcs_tabulate_envelope_objective(
                            physics, x2, &args);
                        if (f2 != 0) {
                                x1 = xmin = x2;
                                f0 = f2;
                        } else {
                                x0 = x2;
                        }
                }
        }
        if (f1 == 0.) {
                double x0, x1;
                if (K >= 1E+01) {
                        /* Apply an exponential decrement */
                        const double xm = exp(xmax);
                        double delta = RESOLUTION, x = xmax, f = f1;
                        while (xm > delta) {
                                x = log(xm - delta);
                                f = dcs_tabulate_envelope_objective(
                                    physics, x, &args);
                                if (f != 0.) break;
                                else delta += delta;
                        }
                        f1 = f;
                        x0 = xmax = x;
                        x1 = log(xm - delta / 2);
                } else {
                        x0 = xmin;
                        x1 = xmax;
                }

                /* Do a bisection */
                while (x1 - x0 > RESOLUTION) {
                        const double x2 = 0.5 * (x0 + x1);
                        const double f2 =
                            dcs_tabulate_envelope_objective(
                                physics, x2, &args);
                        if (f2 != 0) {
                                x0 = xmax = x2;
                                f1 = f2;
                        } else {
                                x1 = x2;
                        }
                }
        }

        /* Search for a maximum (i.e. a minimum of minus f) */
        const double xm = 0.5 * (xmin + xmax);
        const double fm = dcs_tabulate_envelope_objective(physics, xm, &args);

        double fopt0 = 0.;
        math_find_minimum(0, dcs_tabulate_envelope_objective,
            physics, xmin, xm, &f0, &fm, RESOLUTION, 100, &args, NULL,
            &fopt0);

        double fopt1 = 0.;
        math_find_minimum(0, dcs_tabulate_envelope_objective,
            physics, xm, xmax, &fm, &f1, RESOLUTION, 100, &args, NULL,
            &fopt1);

        if ((process == 1) && (K >= 1E+07)) {
                /* There is a very sharp peak close to xmax hard to locate
                 * with standard minimizations partly due to rounding errors.
                 * Let us search a bracketing with a brute force logarithmic
                 * scan.
                 */
#define N_SCAN 7
                double xs[N_SCAN], fs[N_SCAN], fmin = 0.;
                int i, imin = -1;
                for (i = 0; i < N_SCAN; i++) {
                        xs[i] = xmax + log(1. - pow(10, -(i + 1)));
                        fs[i] = dcs_tabulate_envelope_objective(
                            physics, xs[i], &args);
                        if(fs[i] < fmin) {
                                imin = i;
                                fmin = fs[i];
                        }
                }

                if ((imin > 0) && (imin < N_SCAN - 1)) {
                        /* Refine the scan bracketing with a bisection */
                        double fopt2 = 0., xopt2 = 0.;
                        math_find_minimum_bisection(
                            dcs_tabulate_envelope_objective, physics,
                            xs[imin - 1], xs[imin], xs[imin + 1], fs + imin,
                            RESOLUTION, 100, &args, &xopt2, &fopt2);

                        if (fopt2 < fopt1) {
                                fopt1 = fopt2;
                        }
                }
#undef N_SCAN
        }

        double fopt;
        if (fopt0 < fopt1) {
                fopt = fopt0;
        } else {
                fopt = fopt1;
        }
        if (f0 < fopt) {
                fopt = f0;
        }
        if (f1 < fopt) {
                fopt = f1;
        }

        envelope[1] = (float)(-fopt);

#undef RESOLUTION
}

/**
 * Tabulate the DCSs for radiative processes.
 *
 * @param physcis    The physics handle.
 * @param element    The index of the target element.
 * @param error_     The error stream
 * @return On success `PUMAS_RETURN_SUCCESS` is returned otherwise a memory
 * error.
 *
 * This routines tabulates the DCS values over a fixed grid for later evaluation
 * using monotone cubic splines.
 */
enum pumas_return compute_dcs_table(
    struct pumas_physics * physics, int element, struct error_context * error_)
{
        static struct dcs_tabulate_work * work = NULL;
        if (element < 0) {
                /* Free the temporary work data */
                deallocate(work);
                work = NULL;
                return PUMAS_RETURN_SUCCESS;
        } else if (work == NULL) {
                /* Allocate the temporary work data */
                const int n = physics->n_table_dcs;
                size_t size = sizeof(*work) + 3 * n * sizeof(*work->x);
                work = allocate(size);
                if (work == NULL) return ERROR_REGISTER_MEMORY();

                work->x = work->data;
                work->y = work->x + n;
                work->m = work->y + n;

                /* Set the sampling range */
                const int p = DCS_MODEL_N_REVERSE;
                const int m = n - p;

                double xrev;
                if (m > 0) {
                        xrev = DCS_MODEL_X_REVERSE;
                        const double x0 = log(physics->cutoff);
                        const double dx = log(xrev / physics->cutoff) / (m - 1);
                        int i;
                        for (i = 0; i < m; i++) {
                                work->x[i] = x0 + i * dx;
                        }
                } else {
                        xrev = physics->cutoff;
                }

                const double dxmin = DCS_MODEL_DXMIN;
                const double dx = log((DCS_MODEL_MAX_FRACTION - xrev) / dxmin) /
                    ((m > 0) ? p : p - 1);
                int i;
                for (i = 0; i < p; i++) {
                        work->x[n - 1 - i] = log(DCS_MODEL_MAX_FRACTION -
                            dxmin * exp(i * dx));
                }
        }

        /* Tabulate processes for different energies */
        int ip;
        for (ip = 0; ip < N_DEL_PROCESSES - 1; ip++) {
                int row;
                for (row = 0; row < physics->n_energies; row++) {
                        dcs_tabulate_row(physics, ip, element, row, work);
                }

                /* Set the envelope for low energy bins to a constant value */
                int n0 = 0;
                float alpha = 0.f;
                for (row = 0; row < physics->n_energies; row++) {
                        float * env = table_get_dcs_envelope(
                            physics, ip, element, row);
                        if (env[0] > 0.f) {
                                n0 = row;
                                alpha = env[0];
                                break;
                        }
                }
                for (row = 0; row < n0; row++) {
                        float * env = table_get_dcs_envelope(
                            physics, ip, element, row);
                        env[0] = alpha;
                }

                /* Compute the envelope maximum */
                for (row = 0; row < physics->n_energies; row++) {
                        dcs_tabulate_envelope_row(
                            physics, ip, element, row);
                }
        }

        if (element == 0) {
                /* Copy the sampling range */
                const int n = physics->n_table_dcs;
                int i;
                for (i = 0; i < n; i++) {
                        physics->table_DCS_x[i] = (float)work->x[i];
                }
        }

        return PUMAS_RETURN_SUCCESS;
}

/*
 * Low level routines: sampling of the polar angle in a DEL.
 *
 * All the sampling routines below are implementations of the methods
 * described in the Geant4 Physics Reference Manual (PRM).
 */
/**
 * Get a polar angle distrubution given a process index.
 *
 * @param process The process index.
 * @return The polar angle function.
 *
 * `index = 0` corresponds to Bremsstrahlung, `index = 1` to Pair Production,
 * `index = 2` to Photonuclear interactions and `index = 3` to Ionisation.
 */
polar_function_t * polar_get(int process)
{
        polar_function_t * const polar_func[] = { polar_bremsstrahlung,
                polar_pair_production, polar_photonuclear, polar_ionisation };
        return polar_func[process];
}

/**
 * Sample the polar angle in a Bremsstrahlung event.
 *
 * @param Physics Handle for physics tables.
 * @param context The simulation context.
 * @param element The targeted atomic element.
 * @param ki      The initial kinetic energy.
 * @param kf      The final kinetic energy.
 * @return The polar parameters as 0.5 * (1 - cos_theta).
 *
 * The polar angle is sampled using Tsai's DDCS. A rejection sampling method is
 * used in two steps.
 *
 * References:
 *   Y. Tsai, Rev. Mod. Phys. (1974).
 */
double polar_bremsstrahlung(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf)
{
#define MAX_TRIALS 100

        const double m = physics->mass;
        const double E = ki + m;
        const double nu = ki - kf;
        const double y = nu / E;

        const double tmp = 0.5 * m * nu / (E * (E - nu));
        const double mu0 = tmp * tmp;
        const double c1 = (2 * (1. - y) + y * y) * mu0;
        const double c2 = 4 * (1. - y) * mu0 * mu0;

        const double RN = data_nuclear_radius(element->Z, element->A);
        const double tmp2 = m * RN / HBAR_C;
        const double muni = tmp2 * tmp2 / 6.;
        const double muc = sqrt(mu0) - mu0;
        if (muc < 0) return 0;

        double x0;
        {
                const double q = muni;
                const double r = mu0;
                const double qr = q * r;
                x0 = (1. + 2 * qr) * log((1. + qr) / (r + qr)) -
                    (1. - r) * (1 + 2 * q) / (1 + q);
        }

        int i;
        for (i = 0; i < MAX_TRIALS; i++) {
                const double r0 = context->random(context);
                const double r1 = context->random(context);

                /* Randomise over the envelope */
                const double mu = r0 * mu0 / (mu0 + 1. - r0);

                /* First, compare to the unscreened PDF */
                const double mus = mu0 + mu;
                const double mu2 = mus * mus;
                const double d1 = 1 / mu2;
                const double d2 = d1 * d1;
                const double score = r1 * c1 * d1;

                double pdf = c1 * d1 - mu * c2 * d2;
                if (score > pdf) continue;

                /* Second, check the nuclear screening */
                const double l2 = mu2 / (mu0 * mu0);
                const double q = muni * l2;
                const double r = mu0 * l2;
                const double qr = q * r;
                if (qr > 1E+05) continue; /* Above this value the nuclear
                                           * screening is close to zero and
                                           * results are numericaly
                                           * instable using double precision.
                                           */

                const double x = (1. + 2 * qr) * log((1. + qr) / (r + qr)) -
                    (1. - r) * (1 + 2 * q) / (1 + q);

                pdf *= x / x0;
                if (score <= pdf) return mu;
        }

        return 0.; /* Fallback to no scattering in case of failure */
}

/**
 * Sample the polar angle in a Pair Production event.
 *
 * @param Physics Handle for physics tables.
 * @param context The simulation context.
 * @param element The targeted atomic element.
 * @param ki      The initial kinetic energy.
 * @param kf      The final kinetic energy.
 * @return The polar parameters as 0.5 * (1 - cos_theta).
 *
 * The polar angle is sampled assuming a virtual Bremsstrahlung event.
 */
double polar_pair_production(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf)
{
        return polar_bremsstrahlung(physics, context, element, ki, kf);
}

struct photonuclear_polar_parameters {
        double Z;
        double A;
        double K;
        double q;
};

static double photonuclear_polar_objective(
    const struct pumas_physics * physics, double lnQ2, void * params)
{
        ddcs_t * ddcs = dcs_photonuclear_ddcs(physics, NULL);
        struct photonuclear_polar_parameters * p = params;
        const double Q2 = exp(lnQ2);
        return -ddcs(p->Z, p->A, physics->mass, p->K, p->q, Q2) * Q2;
}

/**
 * Sample the polar angle in a photonuclear event.
 *
 * @param Physics Handle for physics tables.
 * @param context The simulation context.
 * @param element The targeted atomic element.
 * @param ki      The initial kinetic energy.
 * @param kf      The final kinetic energy.
 * @return The polar parameters as 0.5 * (1 - cos_theta).
 */
double polar_photonuclear(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf)
{
#define MAX_TRIALS 100
        const double M = 0.5 * (PROTON_MASS + NEUTRON_MASS);
        const double q = ki - kf;
        const double ml = physics->mass;
        const double E = ki + ml;
        const double ml2 = ml * ml;
        const double Q2min = ml2 * (q * q - 0.5 * ml2) / (E * (E - q));
        const double Q2max = 2.0 * M * (q - PION_MASS) - PION_MASS * PION_MASS;
        if ((Q2max < Q2min) | (Q2min < 0)) return 0.;

        struct photonuclear_polar_parameters args = {
                .Z = element->Z,
                .A = element->A,
                .K = ki,
                .q = q
        };
        double fopt = 0., xopt = 0.;
        const double lnQ2min = log(Q2min);
        const double lnQ2max = log(Q2max);
        const int status = math_find_minimum(1, photonuclear_polar_objective,
            physics, lnQ2min, lnQ2max, NULL, NULL, 1E-06, 100, &args, &xopt,
            &fopt);
        fopt = -fopt;
        if (status == -2) return 0.;

        const double x = exp(xopt) / (2 * q * M);
        const double lne = 0.1;
        if ((x <= 0.04) && (xopt + lne < lnQ2max)) {
                /* Look for a second maxima above */
                double f2, x2;
                const int status2 = math_find_minimum(1,
                    photonuclear_polar_objective, physics, xopt + lne, lnQ2max,
                    NULL, NULL, 1E-06, 100, &args, &x2, &f2);
                if (status2 >= 0) {
                        f2 = -f2;
                        if (f2 > fopt) {
                                xopt = x2;
                                fopt = f2;
                        }
                }
        }

        ddcs_t * ddcs = dcs_photonuclear_ddcs(physics, NULL);
        const double rQ2 = lnQ2max - lnQ2min;
        double Q2 = 0.;
        int i;
        for (i = 0; i < MAX_TRIALS; i++) {
                const double u = context->random(context);
                Q2 = Q2min * exp(rQ2 * u);

                const double r =
                    ddcs(element->Z, element->A, ml, ki, q, Q2) * Q2;
                if (context->random(context) * fopt <= r) break;
        }
        if (i == MAX_TRIALS) return 0.;

        const double p = sqrt(ki * (ki + 2 * ml));
        const double E1 = E - q;
        const double eps = ml / E1;
        const double p1 = E1 * sqrt((1. + eps) * (1. - eps));
        const double p2 = p * p1;
        double tmp = p2 + ml * ml - E * E1;
        if (fabs(tmp) <= 3 * DBL_EPSILON * p2) tmp = 0.;
        const double a_mu = 0.5 * tmp / p2;
        const double b_mu = 0.25 / p2;
        double mu = a_mu + b_mu * Q2;
        if (mu < 0) mu = 0.;
        else if (mu > 1.) mu = 1.;

        return mu;

#undef MAX_TRIALS
}

/**
 * Sample the polar angle in an ionisation event.
 *
 * @param Physics Handle for physics tables.
 * @param context The simulation context.
 * @param element The targeted atomic element.
 * @param ki      The initial kinetic energy.
 * @param kf      The final kinetic energy.
 * @return The polar parameters as 0.5 * (1 - cos_theta).
 *
 * The polar angle is set from energy-momentum conservation assuming that the
 * electron is initially at rest. See for example Salvat (2013), NIMB 316.
 */
double polar_ionisation(const struct pumas_physics * physics,
    struct pumas_context * context, const struct atomic_element * element,
    double ki, double kf)
{
        const double nu = ki - kf;
        const double p2 = ki * (ki + 2 * physics->mass);
        const double E = ki + physics->mass;
        const double c = (p2 - nu * (E + ELECTRON_MASS)) /
            sqrt(p2 * (p2 + nu * nu - 2 * nu * E));

        return 0.5 * (1. - c);
}

/*
 * Low level routines: various algorithms for handling some specific
 * mathematic problems.
 */
/**
 * Root solver for a function of a scalar variable.
 *
 * @param Physics Handle for physics tables.
 * @param f       The objective function to resolve.
 * @param xa      The lower bound of the search interval.
 * @param xb      The upper bound of the search interval.
 * @param fa_p    The initial value at *xa* if already computed.
 * @param fb_p    The initial value at *xb* if already computed.
 * @param xtol    The absolute tolerance on the root value.
 * @param rtol    The relative tolerance on the root value.
 * @param params  A handle for passing additional parameters to the
 *                objective function.
 * @param x0      An estimate of the root over `[xa; xb]`.
 * @return On success `0` is returned. Otherwise a negative number.
 *
 * The root is searched for over `[xa; xb]` using Ridder's method
 * (https://en.wikipedia.org/wiki/Ridders%27_method). If initial values of the
 * objective have already been computed they can be passed over as *fa_p* or
 * *fb_p*. Otherwise these values must be `NULL`. The relative tolerance is
 * relative to min(xa, xb).
 */
int math_find_root(
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xb,
    const double * fa_p, const double * fb_p, double xtol, double rtol,
    int max_iter, void * params, double * x0)
{
        /*  Check the initial values. */
        double fa, fb;
        if (fa_p == NULL)
                fa = (*f)(physics, xa, params);
        else
                fa = *fa_p;
        if (fb_p == NULL)
                fb = (*f)(physics, xb, params);
        else
                fb = *fb_p;
        if (fa * fb > 0) {
                *x0 = 0.;
                return -1;
        }
        if (fa == 0) {
                *x0 = xa;
                return 0;
        }
        if (fb == 0) {
                *x0 = xb;
                return 0;
        }

        /* Set the tolerance for the root finding*/
        const double tol =
            xtol + rtol * ((fabs(xa) < fabs(xb)) ? fabs(xa) : fabs(xb));

        /* Do the bracketing using Ridder's update rule. */
        double xn = 0.;
        int i = 0;
        for (i = 0; i < max_iter; i++) {
                double dm = 0.5 * (xb - xa);
                const double xm = xa + dm;
                const double fm = (*f)(physics, xm, params);
                double sgn = (fb > fa) ? 1. : -1.;
                double dn = sgn * dm * fm / sqrt(fm * fm - fa * fb);
                sgn = (dn > 0.) ? 1. : -1.;
                dn = fabs(dn);
                dm = fabs(dm) - 0.5 * tol;
                if (dn < dm) dm = dn;
                xn = xm - sgn * dm;
                const double fn = (*f)(physics, xn, params);
                if (fn * fm < 0.0) {
                        xa = xn;
                        fa = fn;
                        xb = xm;
                        fb = fm;
                } else if (fn * fa < 0.0) {
                        xb = xn;
                        fb = fn;
                } else {
                        xa = xn;
                        fa = fn;
                }
                if (fn == 0.0 || fabs(xb - xa) < tol) {
                        /*  A valid bracketing was found*/
                        *x0 = xn;
                        return 0;
                }
        }

        /* The maximum number of iterations was reached*/
        *x0 = xn;
        return -2;
}

/**
 * Locate the minimum for a function of a scalar variable.
 *
 * @param Physics Handle for physics tables.
 * @param algo    The algorithm to use for the refined search.
 * @param f       The objective function to resolve.
 * @param xa      The lower bound of the search interval.
 * @param xc      The upper bound of the search interval.
 * @param fa_p    The initial value at *xa* if already computed.
 * @param fc_p    The initial value at *xc* if already computed.
 * @param  tol    The relative tolerance on the minimizer.
 * @param params  A handle for passing additional parameters to the
 *                objective function.
 * @param xopt    An estimate of the minimizer over `[xa, xc]` or `NULL`.
 * @param xopt    An estimate of the function minimum over `[xa; xc]` or `NULL`.
 * @return On success `0` or more is returned. Otherwise a negative number.
 *
 * The minimum is searched for over `[xa, xc]` using an initial bisection
 * followed with a refined method as defined by *algo* (see below).
 * If initial values of the objective have already been computed they can be
 * passed over as *fa_p* or *fb_p*. Otherwise these values must be `NULL`. The
 * relative tolerance is relative to `xc - xa`.
 *
 * If algo is `1` then Brent's algorithm is used else a 3 points bisection is
 * used.
 *
 * Warning: A local minimum is found. The algorithm assumes that the objective
 * function has a single minimum over `[xa, xc]`.
 */
int math_find_minimum(int algo,
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xc,
    const double * fa_p, const double * fc_p, double tol, int max_iter,
    void * params, double * xopt, double * fopt)
{

        double fa, fc;
        if (fa_p == NULL)
                fa = (*f)(physics, xa, params);
        else
                fa = *fa_p;
        if (fc_p == NULL)
                fc = (*f)(physics, xc, params);
        else
                fc = *fc_p;

        const double atol = tol * fabs(xc - xa);

        /* Initial bracketing using a bisection */
        double xb = 0, fb = 0;
        int i;
        for (i = 0; i < max_iter; i++) {
                xb = 0.5 * (xa + xc);
                fb = (*f)(physics, xb, params);
                if ((fb < fa) && (fb < fc)) break;

                if (fabs(xc - xa) <= atol) {
                        if (xopt != NULL) *xopt = xb;
                        if (fopt != NULL) *fopt = fb;
                        return i;
                }

                /* It is assumed that fb < max(fa, fb) */
                if (fb < fc) {
                        xc = xb;
                        fc = fb;
                } else {
                        xa = xb;
                        fa = fb;
                }
        }
        if (i == max_iter) {
                if (fa < fb) {
                        fb = fa;
                        xb = xa;
                } else if (fc < fb) {
                        fb = fc;
                        xb = xc;
                }

                if (xopt != NULL) *xopt = xb;
                if (fopt != NULL) *fopt = fb;
                return -1;
        }

        /* Minimum search using Brent's algorithm */
        int status;
        if (algo == 1) {
                status = math_find_minimum_brent(f, physics, xa, xb, xc,
                    &fb, tol, max_iter - i, params, xopt, fopt);
        } else {
                status = math_find_minimum_bisection(f, physics, xa, xb, xc,
                    &fb, tol, max_iter - i, params, xopt, fopt);
        }
        if (status < 0) {
                return -2;
        } else {
                return status + i;
        }

        /*
        while ((xc - xa > atol) && (i < max_iter)) {
                if (xb - xa > xc - xb) {
                        const double x = 0.5 * (xa + xb);
                        const double fi = (*f)(physics, x, params);
                        if (fi < fb) {
                                xc = xb;
                                xb = x;
                                fb = fi;
                        } else {
                                xa = x;
                        }
                } else {
                        const double x = 0.5 * (xb + xc);
                        const double fi = (*f)(physics, x, params);
                        if (fi < fb) {
                                xa = xb;
                                xb = x;
                                fb = fi;
                        } else {
                                xc = x;
                        }
                }
                i++;
        }

        if (xopt != NULL) *xopt = xb;
        if (fopt != NULL) *fopt = fb;
        return (i <= max_iter) ? i : -2;
        */
}

/**
 * Locate the minimum for a function of a scalar variable using Brent's method.
 *
 * @param Physics Handle for physics tables.
 * @param f       The objective function to resolve.
 * @param xa      The lower bound of the search interval.
 * @param xb      The initial minimizer.
 * @param xc      The upper bound of the search interval.
 * @param fb_p    The initial value at *xb* if already computed.
 * @param  tol    The relative tolerance on the minimizer.
 * @param params  A handle for passing additional parameters to the
 *                objective function.
 * @param xopt    An estimate of the minimizer over `[xa, xc]` or `NULL`.
 * @param xopt    An estimate of the function minimum over `[xa; xc]` or `NULL`.
 * @return On success `0` or more is returned. Otherwise a negative number.
 *
 * The minimum is searched for over `[xa, xc]` using Brent's method
 * (https://en.wikipedia.org/wiki/Brent%27s_method). If the initial value of the
 * objective have already been computed they can be passed over as *fb_p*.
 * Otherwise this value must be `NULL`. The relative tolerance is relative to
 * `xc - xa`.
 *
 * Warning: A local minimum is found. The algorithm assumes that the objective
 * function has a single minimum over `[xa, xc]`.
 */
int math_find_minimum_brent(
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xb, double xc,
    const double * fb_p, double tol, int max_iter, void * params, double * xopt,
    double * fopt)
{
#define EPS 1E-15
#define GOLD 0.3819660
#define SIGN(a,b) ((b) >= 0.0 ? fabs(a) : -fabs(a))

        double fb;
        if (fb_p == NULL)
                fb = (*f)(physics, xb, params);
        else
                fb = *fb_p;

        tol *= fabs(xc - xa);

        /* Minimum search using Brent's algorithm */
        double x = xb, w = xb, v = xb;
        double fw = fb, fv = fb, fx = fb;
        double d = 0., e = 0.;
        int i;
        for (i = 0; i < max_iter; i++) {
                const double xm = 0.5 * (xa + xb);
                const double tol1 = tol * fabs(x) + EPS;
                const double tol2 = 2 * tol1;
                if (fabs(x - xm) <= (tol2 - 0.5 * (xb - xa))) {
                        break;
                }
                if (fabs(e) > tol1) {
                        /* Construct a trial parabolic fit */
                        const double r = (x - w) * (fx - fv);
                        double q = (x - v) * (fx - fw);
                        double p = (x - v) * q - (x - w) * r;
                        q = 2.* (q - r);
                        if (q > 0.) p = -p;
                        q = fabs(q);
                        const double etemp = e;
                        e = d;
                        if (fabs(p) >= fabs(0.5 * q * etemp) ||
                            (p <= q * (xa - x)) || (p >= q * (xb - x))) {
                                e = (x >= xm) ? xa - x : xb - x;
                                d = GOLD * e;
                        } else {
                                d = p / q;
                                const double u = x + d;
                                if ((u - xa < tol2) || (xb - u < tol2)) {
                                        d = SIGN(tol1, xm - x);
                                }
                        }
                } else {
                        e = (x >= xm) ? xa - x : xb - x;
                        d = GOLD * e;
                }
                const double u = (fabs(d) >= tol1) ? x + d : x + SIGN(tol1, d);
                const double fu = (*f)(physics, u, params);
                if (fu <= fx) {
                        if (u >= x) {
                                xa = x;
                        } else {
                                xb = x;
                        }

                        v = w, w = x, x = u;
                        fv = fw, fw = fx, fx = fu;
                } else {
                        if (u < x) {
                                xa = u;
                        } else {
                                xb = u;
                        }
                        if ((fu <= fw) || (w == x)) {
                                v = w;
                                w = u;
                                fv = fw;
                                fw = fu;
                        } else if ((fu <= fv) || (v == x) || (v == w)) {
                                v = u;
                                fv = fu;
                        }
                }
        }

        if (xopt != NULL) *xopt = x;
        if (fopt != NULL) *fopt = fx;

        return (i < max_iter) ? i : -1;

#undef EPS
#undef GOLD
#undef SIGN
}

/**
 * Locate the minimum for a function of a scalar variable using a bisection.
 *
 * @param Physics Handle for physics tables.
 * @param f       The objective function to resolve.
 * @param xa      The lower bound of the search interval.
 * @param xb      The initial minimizer.
 * @param xc      The upper bound of the search interval.
 * @param fb_p    The initial value at *xb* if already computed.
 * @param  tol    The relative tolerance on the minimizer.
 * @param params  A handle for passing additional parameters to the
 *                objective function.
 * @param xopt    An estimate of the minimizer over `[xa, xc]` or `NULL`.
 * @param xopt    An estimate of the function minimum over `[xa; xc]` or `NULL`.
 * @return On success `0` or more is returned. Otherwise a negative number.
 *
 * The minimum is searched for over `[xa, xc]` using Brent's method
 * (https://en.wikipedia.org/wiki/Brent%27s_method). If the initial value of the
 * objective have already been computed they can be passed over as *fb_p*.
 * Otherwise this value must be `NULL`. The relative tolerance is relative to
 * `xc - xa`.
 *
 * Warning: A local minimum is found. The algorithm assumes that the objective
 * function has a single minimum over `[xa, xc]`.
 */
int math_find_minimum_bisection(
    double (*f)(const struct pumas_physics * physics, double x, void * params),
    const struct pumas_physics * physics, double xa, double xb, double xc,
    const double * fb_p, double tol, int max_iter, void * params, double * xopt,
    double * fopt)
{
        double fb;
        if (fb_p == NULL)
                fb = (*f)(physics, xb, params);
        else
                fb = *fb_p;

        tol *= fabs(xc - xa);

        int i = 0;
        while ((xc - xa > tol) && (i < max_iter)) {
                if (xb - xa > xc - xb) {
                        const double x = 0.5 * (xa + xb);
                        const double fi = (*f)(physics, x, params);
                        if (fi < fb) {
                                xc = xb;
                                xb = x;
                                fb = fi;
                        } else {
                                xa = x;
                        }
                } else {
                        const double x = 0.5 * (xb + xc);
                        const double fi = (*f)(physics, x, params);
                        if (fi < fb) {
                                xa = xb;
                                xb = x;
                                fb = fi;
                        } else {
                                xc = x;
                        }
                }
                i++;
        }

        if (xopt != NULL) *xopt = xb;
        if (fopt != NULL) *fopt = fb;
        return (i <= max_iter) ? i : -1;
}

/**
 * Iterator function for integration with a Gaussian quadrature.
 *
 * @param n  The minimum number of integration points.
 * @param p1 The integration range lower bound or the sampling point.
 * @param p2 The integration range upper bound or the sampling weight.
 * @return The number of points used for the integration or a return code.
 *
 * If *n* is strictly positive the iterator is initialised for a new
 * integration with at least *n* points. The integration range is `[p1, p2]`
 * and at return the number of integration points is returned.
 *
 * Otherwise, the next integration point is provided in *p1* and *p2* is
 * filled with the corresponding weight. If all integration steps have been
 * done `1` is returned, otherwise `0`.
 */
int math_gauss_quad(int n, double * p1, double * p2)
{
/*
 * Coefficients for the Gaussian quadrature from:
 * https://pomax.github.io/bezierinfo/legendre-gauss.html.
 */
#define N_GQ 6
        static const double * xGQ = NULL;
        static const double * wGQ = NULL;

        /* Initialisation step. */
        static int i, j, n_itv;
        static double h;
        static double x0;
        if (n > 0) {
                if (xGQ == NULL)
                        math_gauss_quad_coefficients(N_GQ, &xGQ, &wGQ);

                n_itv = (n + N_GQ - 1) / N_GQ;
                i = j = 0;
                h = (*p2 - (x0 = *p1)) / n_itv;
                return N_GQ * n_itv;
        }

        /* Iteration step. */
        if (i == n_itv) return 1;

        *p1 = x0 + xGQ[j] * h;
        *p2 = wGQ[j] * h;

        if (++j == N_GQ) {
                i++;
                j = 0;
                x0 += h;
        }
        return 0;

#undef N_GQ
}

/**
 * Coefficients for a Gaussian quadrature or order n
 *
 * @param n       The number of integration points.
 * @param xGQ     The integration nodes.
 * @param wGQ     The integration weights.
 *
 * The coefficients for the Gaussian quadratures are taken from:
 * https://pomax.github.io/bezierinfo/legendre-gauss.html.
 */
void math_gauss_quad_coefficients(
    int n, const double ** xGQ, const double ** wGQ)
{
        static const double x[] = {
            /* N_GQ = 1 */
            0.5000000000000000,
            /* N_GQ = 2 */
            0.2113248654051871, 0.7886751345948129,
            /* N_GQ = 3 */
            0.1127016653792583, 0.5000000000000000, 0.8872983346207417,
            /* N_GQ = 4 */
            0.0694318442029737, 0.3300094782075719, 0.6699905217924281,
            0.9305681557970262,
            /* N_GQ = 5 */
            0.0469100770306680, 0.2307653449471584, 0.5000000000000000,
            0.7692346550528415, 0.9530899229693319,
            /* N_GQ = 6 */
            0.0337652428984240, 0.1693953067668678, 0.3806904069584016,
            0.6193095930415985, 0.8306046932331322, 0.9662347571015760,
            /* N_GQ = 7 */
            0.0254460438286208, 0.1292344072003028, 0.2970774243113014,
            0.5000000000000000, 0.7029225756886985, 0.8707655927996972,
            0.9745539561713792,
            /* N_GQ = 8 */
            0.0198550717512319, 0.1016667612931866, 0.2372337950418355,
            0.4082826787521751, 0.5917173212478249, 0.7627662049581645,
            0.8983332387068134, 0.9801449282487682,
            /* N_GQ = 9 */
            0.0159198802461870, 0.0819844463366821, 0.1933142836497048,
            0.3378732882980955, 0.5000000000000000, 0.6621267117019045,
            0.8066857163502952, 0.9180155536633179, 0.9840801197538130,
            /* N_GQ = 10 */
            0.0130467357414141, 0.0674683166555077, 0.1602952158504878,
            0.2833023029353764, 0.4255628305091844, 0.5744371694908156,
            0.7166976970646236, 0.8397047841495122, 0.9325316833444923,
            0.9869532642585859,
            /* N_GQ = 11 */
            0.0108856709269715, 0.0564687001159523, 0.1349239972129753,
            0.2404519353965941, 0.3652284220238275, 0.5000000000000000,
            0.6347715779761725, 0.7595480646034058, 0.8650760027870247,
            0.9435312998840477, 0.9891143290730284,
            /* N_GQ = 12 */
            0.0092196828766404, 0.0479413718147625, 0.1150486629028477,
            0.2063410228566913, 0.3160842505009099, 0.4373832957442655,
            0.5626167042557344, 0.6839157494990901, 0.7936589771433087,
            0.8849513370971523, 0.9520586281852375, 0.9907803171233596
        };

        static const double w[] = {
            /* N_GQ = 1 */
            1.0000000000000000,
            /* N_GQ = 2 */
            0.5000000000000000, 0.5000000000000000,
            /* N_GQ = 3 */
            0.2777777777777778, 0.4444444444444444, 0.2777777777777778,
            /* N_GQ = 4 */
            0.1739274225687269, 0.3260725774312731, 0.3260725774312731,
            0.1739274225687269,
            /* N_GQ = 5 */
            0.1184634425280946, 0.2393143352496833, 0.2844444444444444,
            0.2393143352496833, 0.1184634425280946,
            /* N_GQ = 6 */
            0.0856622461895852, 0.1803807865240693, 0.2339569672863455,
            0.2339569672863455, 0.1803807865240693, 0.0856622461895852,
            /* N_GQ = 7 */
            0.0647424830844349, 0.1398526957446383, 0.1909150252525595,
            0.2089795918367347, 0.1909150252525595, 0.1398526957446383,
            0.0647424830844349,
            /* N_GQ = 8 */
            0.0506142681451881, 0.1111905172266872, 0.1568533229389436,
            0.1813418916891810, 0.1813418916891810, 0.1568533229389436,
            0.1111905172266872, 0.0506142681451881,
            /* N_GQ = 9 */
            0.0406371941807872, 0.0903240803474287, 0.1303053482014677,
            0.1561735385200015, 0.1651196775006299, 0.1561735385200015,
            0.1303053482014677, 0.0903240803474287, 0.0406371941807872,
            /* N_GQ = 10 */
            0.0333356721543440, 0.0747256745752903, 0.1095431812579910,
            0.1346333596549981, 0.1477621123573765, 0.1477621123573765,
            0.1346333596549981, 0.1095431812579910, 0.0747256745752903,
            0.0333356721543440,
            /* N_GQ = 11 */
            0.0278342835580868, 0.0627901847324523, 0.0931451054638671,
            0.1165968822959952, 0.1314022722551233, 0.1364625433889503,
            0.1314022722551233, 0.1165968822959952, 0.0931451054638671,
            0.0627901847324523, 0.0278342835580868,
            /* N_GQ = 12 */
            0.0235876681932559, 0.0534696629976592, 0.0800391642716731,
            0.1015837133615330, 0.1167462682691774, 0.1245735229067014,
            0.1245735229067014, 0.1167462682691774, 0.1015837133615330,
            0.0800391642716731, 0.0534696629976592, 0.0235876681932559
        };

        /* Return the requested coefficients */
        n *= n - 1;
        n /= 2;
        *xGQ = x + n;
        *wGQ = w + n;
}

/**
 * Initialise arrays for a single pass Gaussian quadrature.
 *
 * @param n       The number of integration points.
 * @param xmin    The integration range lower bound.
 * @param xmax    The integration range upper bound.
 * @param xmin    The integration range lower bound.
 * @param xGQ     The integration nodes.
 * @param wGQ     The integration weights.
 *
 * Warning: the xGQ and wGQ arrays must be of size n or more.
 */
void math_gauss_quad_initialise(
   int n, double xmin, double xmax, int logscale, double * xGQ, double * wGQ)
{
        const double *x, *w;
        math_gauss_quad_coefficients(n, &x, &w);
        memcpy(xGQ, x, n * sizeof(*xGQ));
        memcpy(wGQ, w, n * sizeof(*wGQ));

        if (logscale) {
                xmin = log(xmin);
                xmax = log(xmax);

                const double dx = xmax - xmin;
                int i;
                for (i = 0; i < n; i++) {
                        const double xi = exp(xmin + dx * xGQ[i]);
                        xGQ[i] = xi;
                        wGQ[i] *= dx * xi;
                }
        } else {
                const double dx = xmax - xmin;
                int i;
                for (i = 0; i < n; i++) {
                        xGQ[i] = xmin + dx * xGQ[i];
                        wGQ[i] *= dx;
                }
        }
}

/** Dilogarithm for real valued arguments
 *
 * Ref: CERNLIB RDILOG function (C332)
 * http://cds.cern.ch/record/2050865
 */
static double math_dilog(double x)
{
        const double C[20] = {
             0.42996693560813697,  0.40975987533077105, -0.01858843665014592,
             0.00145751084062268, -0.00014304184442340,  0.00001588415541880,
            -0.00000190784959387,  0.00000024195180854, -0.00000003193341274,
             0.00000000434545063, -0.00000000060578480,  0.00000000008612098,
            -0.00000000001244332,  0.00000000000182256, -0.00000000000027007,
             0.00000000000004042, -0.00000000000000610,  0.00000000000000093,
            -0.00000000000000014,  0.00000000000000002};

        const double PI3 = M_PI * M_PI / 3.;
        const double PI6 = M_PI * M_PI / 6.;
        const double PI12 = M_PI * M_PI / 12.;

        if (x == 10) {
                return PI6;
        } else if ((x == -1.) || (1. - x == 0.)) {
                return -PI12;
        } else {
                double T = -x;
                double A, S, Y;

                if (T <= -2) {
                        Y = -1. / (1. + T);
                        S = 1.;
                        A = -PI3 + 0.5 * (pow(log(-T), 2.) -
                            pow(log(1. + 1. / T), 2.));
                } else if (T < -1.) {
                        Y = -1. - T;
                        S = -1.;
                        A = log(-T);
                        A = -PI6 + A * (A + log(1. + 1. / T));
                } else if (T <= -0.5) {
                        Y = -(1. + T) / T;
                        S = 1.;
                        A = log(-T);
                        A = -PI6 + A * (-0.5 * A + log(1. + T));
                } else if (T < 0.) {
                        Y = -T / (1. + T);
                        S = -1.;
                        A = 0.5 * pow(log(1 + T), 2.);
                } else if (T <= 1.) {
                        Y = T;
                        S = 1.;
                        A = 0.;
                } else {
                        Y = 1. / T;
                        S = -1.;
                        A = PI6 + 0.5 * pow(log(T), 2);
                }

                const double H = Y + Y - 1;
                const double ALFA = H + H;
                double B0, B1 = 0., B2 = 0.;
                int i;
                for (i = 19; i >= 0; i--) {
                        B0 = C[i] + ALFA * B1 - B2;
                        B2 = B1;
                        B1 = B0;
                }

                return -(S * (B0 - H * B2) + A);
        }
}

/**
 * Routines for computing energy loss tabulations.
 */

/** Raw data for atomic shells
 *
 * References:
 *     Carlson, Photoelectron and Auger Spectroscopy (1976),
 *     CRC, Handbook of Chemistry and Physics (1993).
 *
 * Generated from Geant4 10.7 (G4AtomicShells).
 */
static unsigned short atomic_shell_index[] = {
       0,    1,    2,    4,    6,    9,   12,   16,   20,   23,   27,   32,
      37,   43,   49,   55,   61,   67,   74,   82,   90,   99,  108,  117,
     126,  135,  144,  153,  163,  173,  183,  194,  205,  216,  227,  238,
     250,  263,  276,  290,  304,  318,  332,  346,  360,  374,  389,  404,
     419,  435,  451,  467,  483,  499,  516,  534,  552,  571,  590,  609,
     628,  647,  666,  685,  705,  724,  743,  762,  781,  800,  820,  841,
     862,  883,  904,  925,  946,  967,  988, 1010, 1032, 1055, 1078, 1101,
    1124, 1148, 1172, 1197, 1222, 1248, 1274, 1301, 1328, 1355, 1381, 1407,
    1434, 1461, 1487, 1513, 1539
};

static unsigned char atomic_shell_occupancy[] = {
     1,  2,  2,  1,  2,  2,  2,  2,  1,  2,  2,  2,  2,  2,  2,  1,  2,  2,
     2,  2,  2,  2,  5,  2,  2,  2,  4,  2,  2,  2,  4,  1,  2,  2,  2,  4,
     2,  2,  2,  2,  4,  2,  1,  2,  2,  2,  4,  2,  2,  2,  2,  2,  4,  2,
     3,  2,  2,  2,  4,  2,  4,  2,  2,  2,  4,  2,  5,  2,  2,  2,  4,  2,
     2,  4,  2,  2,  2,  4,  2,  2,  4,  1,  2,  2,  2,  4,  2,  2,  4,  2,
     2,  2,  2,  4,  2,  2,  4,  1,  2,  2,  2,  2,  4,  2,  2,  4,  2,  2,
     2,  2,  2,  4,  2,  2,  4,  3,  2,  2,  2,  2,  4,  2,  2,  4,  4,  2,
     2,  2,  2,  4,  2,  2,  4,  5,  2,  2,  2,  2,  4,  2,  2,  4,  6,  2,
     2,  2,  2,  4,  2,  2,  4,  7,  2,  2,  2,  2,  4,  2,  2,  4,  4,  4,
     2,  2,  2,  2,  4,  2,  2,  4,  4,  5,  2,  2,  2,  2,  4,  2,  2,  4,
     4,  6,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  1,  2,  2,  2,  4,
     2,  2,  4,  4,  6,  2,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  3,
     2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  4,  2,  2,  2,  4,  2,  2,  4,
     4,  6,  2,  5,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  2,  2,
     2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  1,  2,  2,  2,  4,  2,  2,  4,
     4,  6,  2,  2,  4,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,
     2,  1,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  2,  2,  2,  2,
     2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  3,  2,  2,  2,  2,  4,  2,  2,
     4,  4,  6,  2,  2,  4,  4,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,
     2,  4,  5,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  6,  2,
     2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  7,  2,  2,  2,  2,  4,
     2,  2,  4,  4,  6,  2,  2,  4,  4,  4,  2,  2,  2,  2,  4,  2,  2,  4,
     4,  6,  2,  2,  4,  4,  5,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,
     2,  4,  4,  6,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,
     6,  2,  1,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,
     2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  3,  2,
     2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  4,  2,  2,  2,
     4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  5,  2,  2,  2,  4,  2,
     2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  2,  2,  2,  4,  2,  2,
     4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  1,  2,  2,  2,  4,  2,  2,
     4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  2,  2,  2,  2,  4,  2,  2,
     4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  1,  2,  2,  2,  2,  4,  2,
     2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  2,  2,  2,  2,  2,  4,
     2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  3,  2,  2,  2,  2,
     4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  4,  2,  2,  2,
     2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  5,  2,  2,
     2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  6,  2,
     2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  7,
     2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,
     2,  7,  1,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,
     2,  4,  9,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,
     2,  2,  4, 10,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,
     6,  2,  2,  4,  2, 11,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,
     4,  6,  2,  2,  4,  2, 12,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,
     4,  4,  6,  2,  2,  4, 13,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,
     2,  4,  4,  6,  2,  2,  4,  6,  8,  2,  2,  2,  2,  4,  2,  2,  4,  4,
     6,  2,  2,  4,  4,  6,  2,  2,  4,  6,  8,  2,  1,  2,  2,  2,  4,  2,
     2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  6,  8,  2,  2,  2,  2,
     2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,  6,  8,  3,
     2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  2,  4,
     6,  8,  4,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,
     2,  2,  6,  8,  4,  5,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,
     4,  4,  6,  2,  2,  6,  8,  4,  6,  2,  2,  2,  2,  4,  2,  2,  4,  4,
     6,  2,  2,  4,  4,  6,  2,  6,  2,  8,  4,  7,  2,  2,  2,  2,  4,  2,
     2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  6,  8,  2,  4,  9,  1,  2,  2,
     2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  6,  8,  2,  4,  4,
     6,  1,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  2,  6,
     8,  2,  4,  4,  6,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,
     4,  6,  2,  6,  8,  2,  4,  4,  6,  2,  1,  2,  2,  2,  4,  2,  2,  4,
     4,  6,  2,  2,  4,  4,  6,  2,  6,  8,  2,  4,  4,  6,  2,  2,  2,  2,
     2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  2,  8,  2,  4,  4,
     6,  2,  3,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,
     8,  2,  2,  4,  4,  6,  2,  4,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,
     2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,  2,  3,  2,  2,  2,  4,
     2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,
     2,  4,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  8,
     2,  2,  4,  4,  6,  2,  2,  4,  1,  2,  2,  2,  4,  2,  2,  4,  4,  6,
     2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,  2,  4,  2,  2,  2,
     2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,
     6,  2,  2,  4,  2,  1,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,
     4,  6,  6,  8,  2,  2,  4,  4,  6,  2,  2,  4,  2,  2,  2,  2,  2,  4,
     2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,
     2,  4,  2,  1,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,
     6,  6,  8,  2,  2,  4,  4,  6,  2,  2,  4,  1,  3,  2,  2,  2,  2,  4,
     2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,
     2,  4,  4,  1,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,
     6,  6,  8,  2,  2,  4,  4,  6,  2,  2,  4,  6,  2,  2,  2,  2,  4,  2,
     2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,  2,
     4,  7,  2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,
     8,  2,  2,  4,  4,  6,  2,  2,  4,  7,  2,  1,  2,  2,  2,  4,  2,  2,
     4,  4,  6,  2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,  2,  4,
     8,  2,  1,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,
     8,  2,  2,  4,  4,  6,  2,  2,  4, 10,  2,  2,  2,  2,  4,  2,  2,  4,
     4,  6,  2,  2,  4,  4,  6,  6,  8,  2,  2,  4,  4,  6,  2,  2,  4, 11,
     2,  2,  2,  2,  4,  2,  2,  4,  4,  6,  2,  2,  4,  4,  6,  6,  8,  2,
     2,  4,  4,  6,  2,  2,  4, 12,  2
};

static float atomic_shell_energy[] = {
        13.6f,     24.6f,     58.0f,      5.4f,    115.0f,      9.3f,    192.0f,
        12.9f,      8.3f,    288.0f,     16.6f,     11.3f,    403.0f,     37.3f,
        20.3f,     14.5f,    543.1f,     41.6f,     28.5f,     13.6f,    696.7f,
        37.9f,     17.4f,    870.1f,     48.5f,     21.7f,     21.6f,   1075.0f,
        66.0f,     34.0f,     34.0f,      5.1f,   1308.0f,     92.0f,     54.0f,
        54.0f,      7.7f,   1564.0f,    121.0f,     77.0f,     77.0f,     10.6f,
         6.0f,   1844.0f,    154.0f,    104.0f,    104.0f,     13.5f,      8.2f,
      2148.0f,    191.0f,    135.0f,    134.0f,     16.1f,     10.5f,   2476.0f,
       232.0f,    170.0f,    168.0f,     20.2f,     10.4f,   2829.0f,    277.0f,
       208.0f,    206.0f,     24.5f,     13.0f,   3206.3f,    326.5f,    250.6f,
       248.5f,     29.2f,     15.9f,     15.8f,   3610.0f,    381.0f,    299.0f,
       296.0f,     37.0f,     19.0f,     18.7f,      4.3f,   4041.0f,    441.0f,
       353.0f,    349.0f,     46.0f,     28.0f,     28.0f,      6.1f,   4494.0f,
       503.0f,    408.0f,    403.0f,     55.0f,     33.0f,     33.0f,      8.0f,
         6.5f,   4966.0f,    567.0f,    465.0f,    459.0f,     64.0f,     39.0f,
        38.0f,      8.0f,      6.8f,   5465.0f,    633.0f,    525.0f,    518.0f,
        72.0f,     44.0f,     43.0f,      8.0f,      6.7f,   5989.0f,    702.0f,
       589.0f,    580.0f,     80.0f,     49.0f,     48.0f,      8.2f,      6.8f,
      6539.0f,    755.0f,    656.0f,    645.0f,     89.0f,     55.0f,     53.0f,
         9.0f,      7.4f,   7112.0f,    851.0f,    726.0f,    713.0f,     98.0f,
        61.0f,     59.0f,      9.0f,      7.9f,   7709.0f,    931.0f,    800.0f,
       785.0f,    107.0f,     68.0f,     66.0f,      9.0f,      7.9f,   8333.0f,
      1015.0f,    877.0f,    860.0f,    117.0f,     75.0f,     73.0f,     10.0f,
        10.0f,      7.6f,   8979.0f,   1103.0f,    958.0f,    938.0f,    127.0f,
        82.0f,     80.0f,     11.0f,     10.4f,      7.7f,   9659.0f,   1198.0f,
      1047.0f,   1024.0f,    141.0f,     94.0f,     91.0f,     12.0f,     11.2f,
         9.4f,  10367.0f,   1302.0f,   1146.0f,   1119.0f,    162.0f,    111.0f,
       107.0f,     21.0f,     20.0f,     11.0f,      6.0f,  11103.0f,   1413.0f,
      1251.0f,   1220.0f,    184.0f,    130.0f,    125.0f,     33.0f,     32.0f,
        14.3f,      7.9f,  11867.0f,   1531.0f,   1362.0f,   1327.0f,    208.0f,
       151.0f,    145.0f,     46.0f,     45.0f,     17.0f,      9.8f,  12658.0f,
      1656.0f,   1479.0f,   1439.0f,    234.0f,    173.0f,    166.0f,     61.0f,
        60.0f,     20.1f,      9.8f,  13474.0f,   1787.0f,   1602.0f,   1556.0f,
       262.0f,    197.0f,    189.0f,     77.0f,     76.0f,     23.8f,     11.8f,
     14326.0f,   1924.6f,   1730.9f,   1678.4f,    292.8f,    222.2f,    214.4f,
        95.0f,     93.8f,     27.5f,     14.7f,     14.0f,  15200.0f,   2068.0f,
      1867.0f,   1807.0f,    325.0f,    251.0f,    242.0f,    116.0f,    114.0f,
        32.0f,     16.0f,     15.3f,      4.2f,  16105.0f,   2219.0f,   2010.0f,
      1943.0f,    361.0f,    283.0f,    273.0f,    139.0f,    137.0f,     40.0f,
        23.0f,     22.0f,      5.7f,  17038.0f,   2375.0f,   2158.0f,   2083.0f,
       397.0f,    315.0f,    304.0f,    163.0f,    161.0f,     48.0f,     30.0f,
        29.0f,      6.5f,      6.4f,  17998.0f,   2536.0f,   2311.0f,   2227.0f,
       434.0f,    348.0f,    335.0f,    187.0f,    185.0f,     56.0f,     35.0f,
        33.0f,      8.6f,      6.8f,  18986.0f,   2702.0f,   2469.0f,   2375.0f,
       472.0f,    382.0f,    367.0f,    212.0f,    209.0f,     62.0f,     40.0f,
        38.0f,      7.2f,      6.9f,  20000.0f,   2872.0f,   2632.0f,   2527.0f,
       511.0f,    416.0f,    399.0f,    237.0f,    234.0f,     68.0f,     45.0f,
        42.0f,      8.6f,      7.1f,  21044.0f,   3048.0f,   2800.0f,   2683.0f,
       551.0f,    451.0f,    432.0f,    263.0f,    259.0f,     74.0f,     49.0f,
        45.0f,      8.6f,      7.3f,  22117.0f,   3230.0f,   2973.0f,   2844.0f,
       592.0f,    488.0f,    466.0f,    290.0f,    286.0f,     81.0f,     53.0f,
        49.0f,      8.5f,      7.4f,  23220.0f,   3418.0f,   3152.0f,   3010.0f,
       634.0f,    526.0f,    501.0f,    318.0f,    313.0f,     87.0f,     58.0f,
        53.0f,      9.6f,      7.5f,  24350.0f,   3611.0f,   3337.0f,   3180.0f,
       677.0f,    565.0f,    537.0f,    347.0f,    342.0f,     93.0f,     63.0f,
        57.0f,      8.8f,      8.3f,      7.5f,  25514.0f,   3812.0f,   3530.0f,
      3357.0f,    724.0f,    608.0f,    577.0f,    379.0f,    373.0f,    101.0f,
        69.0f,     63.0f,     11.0f,     10.0f,      7.6f,  26711.0f,   4022.0f,
      3732.0f,   3542.0f,    775.0f,    655.0f,    621.0f,    415.0f,    408.0f,
       112.0f,     78.0f,     71.0f,     14.0f,     13.0f,      9.0f,  27940.0f,
      4242.0f,   3943.0f,   3735.0f,    830.0f,    707.0f,    669.0f,    455.0f,
       447.0f,    126.0f,     90.0f,     82.0f,     21.0f,     20.0f,     10.0f,
         5.8f,  29200.0f,   4469.0f,   4160.0f,   3933.0f,    888.0f,    761.0f,
       719.0f,    497.0f,    489.0f,    141.0f,    102.0f,     93.0f,     29.0f,
        28.0f,     12.0f,      7.3f,  30419.0f,   4698.0f,   4385.0f,   4137.0f,
       949.0f,    817.0f,    771.0f,    542.0f,    533.0f,    157.0f,    114.0f,
       104.0f,     38.0f,     37.0f,     15.0f,      8.6f,  31814.0f,   4939.0f,
      4612.0f,   4347.0f,   1012.0f,    876.0f,    825.0f,    589.0f,    578.0f,
       174.0f,    127.0f,    117.0f,     48.0f,     46.0f,     17.8f,      9.0f,
     33169.0f,   5188.0f,   4852.0f,   4557.0f,   1078.0f,    937.0f,    881.0f,
       638.0f,    626.0f,    193.0f,    141.0f,    131.0f,     58.0f,     56.0f,
        20.6f,     10.4f,  34570.0f,   5460.0f,   5110.0f,   4790.0f,   1148.7f,
      1002.1f,    940.6f,    689.0f,    676.4f,    213.2f,    157.0f,    145.5f,
        69.5f,     67.5f,     23.4f,     13.4f,     12.1f,  35985.0f,   5714.0f,
      5359.0f,   5012.0f,   1220.0f,   1068.0f,   1000.0f,    742.0f,    728.0f,
       233.0f,    174.0f,    164.0f,     81.0f,     79.0f,     25.0f,     14.0f,
        12.3f,      3.9f,  37441.0f,   5989.0f,   5624.0f,   5247.0f,   1293.0f,
      1138.0f,   1063.0f,    797.0f,    782.0f,    254.0f,    193.0f,    181.0f,
        94.0f,     92.0f,     31.0f,     18.0f,     16.0f,      5.2f,  38925.0f,
      6266.0f,   5891.0f,   5483.0f,   1365.0f,   1207.0f,   1124.0f,    851.0f,
       834.0f,    273.0f,    210.0f,    196.0f,    105.0f,    103.0f,     36.0f,
        22.0f,     19.0f,      5.8f,      5.6f,  40443.0f,   6548.0f,   6164.0f,
      5723.0f,   1437.0f,   1275.0f,   1184.0f,    903.0f,    885.0f,    291.0f,
       225.0f,    209.0f,    114.0f,    111.0f,     39.0f,     25.0f,     22.0f,
         6.0f,      5.7f,  41991.0f,   6835.0f,   6440.0f,   5964.0f,   1509.0f,
      1342.0f,   1244.0f,    954.0f,    934.0f,    307.0f,    238.0f,    220.0f,
       121.0f,    117.0f,     41.0f,     27.0f,     24.0f,      6.0f,      5.4f,
     43569.0f,   7126.0f,   6722.0f,   6208.0f,   1580.0f,   1408.0f,   1303.0f,
      1005.0f,    983.0f,    321.0f,    250.0f,    230.0f,    126.0f,    122.0f,
        42.0f,     28.0f,     25.0f,      6.0f,      5.5f,  45184.0f,   7428.0f,
      7013.0f,   6459.0f,   1653.0f,   1476.0f,   1362.0f,   1057.0f,   1032.0f,
       325.0f,    261.0f,    240.0f,    131.0f,    127.0f,     43.0f,     28.0f,
        25.0f,      6.0f,      5.5f,  46834.0f,   7737.0f,   7312.0f,   6716.0f,
      1728.0f,   1546.0f,   1422.0f,   1110.0f,   1083.0f,    349.0f,    273.0f,
       251.0f,    137.0f,    132.0f,     44.0f,     29.0f,     25.0f,      6.0f,
         5.6f,  48519.0f,   8052.0f,   7617.0f,   6977.0f,   1805.0f,   1618.0f,
      1484.0f,   1164.0f,   1135.0f,    364.0f,    286.0f,    262.0f,    143.0f,
       137.0f,     45.0f,     30.0f,     26.0f,      6.0f,      5.7f,  50239.0f,
      8376.0f,   7930.0f,   7243.0f,   1884.0f,   1692.0f,   1547.0f,   1220.0f,
      1189.0f,    380.0f,    300.0f,    273.0f,    150.0f,    143.0f,     46.0f,
        31.0f,     27.0f,      6.2f,      6.0f,      6.0f,  51996.0f,   8708.0f,
      8252.0f,   7514.0f,   1965.0f,   1768.0f,   1612.0f,   1277.0f,   1243.0f,
       398.0f,    315.0f,    285.0f,    157.0f,    150.0f,     48.0f,     32.0f,
        28.0f,      6.0f,      5.8f,  53789.0f,   9046.0f,   8581.0f,   7790.0f,
      2048.0f,   1846.0f,   1678.0f,   1335.0f,   1298.0f,    416.0f,    331.0f,
       297.0f,    164.0f,    157.0f,     50.0f,     33.0f,     28.0f,      6.0f,
         5.9f,  55618.0f,   9394.0f,   8918.0f,   8071.0f,   2133.0f,   1926.0f,
      1746.0f,   1395.0f,   1354.0f,    434.0f,    348.0f,    310.0f,    172.0f,
       164.0f,     52.0f,     34.0f,     29.0f,      6.0f,      6.0f,  57486.0f,
      9751.0f,   9264.0f,   8358.0f,   2220.0f,   2008.0f,   1815.0f,   1456.0f,
      1412.0f,    452.0f,    365.0f,    323.0f,    181.0f,    172.0f,     54.0f,
        35.0f,     30.0f,      6.1f,      6.0f,  59390.0f,  10116.0f,   9617.0f,
      8648.0f,   2309.0f,   2092.0f,   1885.0f,   1518.0f,   1471.0f,    471.0f,
       382.0f,    336.0f,    190.0f,    181.0f,     56.0f,     36.0f,     30.0f,
         7.0f,      6.2f,  61332.0f,  10486.0f,   9978.0f,   8944.0f,   2401.0f,
      2178.0f,   1956.0f,   1580.0f,   1531.0f,    490.0f,    399.0f,    349.0f,
       200.0f,    190.0f,     58.0f,     37.0f,     31.0f,      8.0f,      7.0f,
         6.2f,  63314.0f,  10870.0f,  10349.0f,   9244.0f,   2499.0f,   2270.0f,
      2032.0f,   1647.0f,   1596.0f,    514.0f,    420.0f,    366.0f,    213.0f,
       202.0f,     62.0f,     39.0f,     32.0f,     13.0f,     12.0f,      7.0f,
         6.6f,  65351.0f,  11271.0f,  10739.0f,   9561.0f,   2604.0f,   2369.0f,
      2113.0f,   1720.0f,   1665.0f,    542.0f,    444.0f,    386.0f,    229.0f,
       217.0f,     68.0f,     43.0f,     35.0f,     21.0f,     20.0f,      7.5f,
         7.0f,  67416.0f,  11682.0f,  11136.0f,   9881.0f,   2712.0f,   2472.0f,
      2197.0f,   1796.0f,   1737.0f,    570.0f,    469.0f,    407.0f,    245.0f,
       232.0f,     74.0f,     47.0f,     38.0f,     30.0f,     28.0f,      8.3f,
         7.9f,  69525.0f,  12100.0f,  11544.0f,  10207.0f,   2823.0f,   2577.0f,
      2283.0f,   1874.0f,   1811.0f,    599.0f,    495.0f,    428.0f,    261.0f,
       248.0f,     80.0f,     51.0f,     41.0f,     38.0f,     36.0f,      9.0f,
         8.0f,  71676.0f,  12527.0f,  11959.0f,  10535.0f,   2937.0f,   2686.0f,
      2371.0f,   1953.0f,   1887.0f,    629.0f,    522.0f,    450.0f,    278.0f,
       264.0f,     86.0f,     56.0f,     47.0f,     45.0f,     45.0f,      9.6f,
         7.9f,  73871.0f,  12968.0f,  12385.0f,  10871.0f,   3054.0f,   2797.0f,
      2461.0f,   2035.0f,   1964.0f,    660.0f,    551.0f,    473.0f,    295.0f,
       280.0f,     92.0f,     61.0f,     56.0f,     54.0f,     49.0f,      9.6f,
         8.5f,  76111.0f,  13419.0f,  12824.0f,  11215.0f,   3175.0f,   2912.0f,
      2554.0f,   2119.0f,   2044.0f,    693.0f,    581.0f,    497.0f,    314.0f,
       298.0f,     99.0f,     67.0f,     66.0f,     64.0f,     53.0f,      9.6f,
         9.1f,  78395.0f,  13880.0f,  13273.0f,  11564.0f,   3300.0f,   3030.0f,
      2649.0f,   2206.0f,   2126.0f,    727.0f,    612.0f,    522.0f,    335.0f,
       318.0f,    106.0f,     78.0f,     75.0f,     71.0f,     57.0f,      9.6f,
         9.0f,  80725.0f,  14353.0f,  13734.0f,  11919.0f,   3430.0f,   3153.0f,
      2748.0f,   2295.0f,   2210.0f,    764.0f,    645.0f,    548.0f,    357.0f,
       339.0f,    114.0f,     91.0f,     87.0f,     76.0f,     61.0f,     12.5f,
        11.1f,      9.2f,  83102.0f,  14839.0f,  14209.0f,  12284.0f,   3567.0f,
      3283.0f,   2852.0f,   2390.0f,   2300.0f,    806.0f,    683.0f,    579.0f,
       382.0f,    363.0f,    125.0f,    107.0f,    103.0f,     85.0f,     68.0f,
        14.0f,     12.0f,     10.4f,  85530.0f,  15347.0f,  14698.0f,  12658.0f,
      3710.0f,   3420.0f,   2961.0f,   2490.0f,   2394.0f,    852.0f,    726.0f,
       615.0f,    411.0f,    391.0f,    139.0f,    127.0f,    123.0f,     98.0f,
        79.0f,     21.0f,     19.0f,      8.0f,      6.1f,  88005.0f,  15861.0f,
     15200.0f,  13055.0f,   3857.0f,   3560.0f,   3072.0f,   2592.0f,   2490.0f,
       899.0f,    769.0f,    651.0f,    441.0f,    419.0f,    153.0f,    148.0f,
       144.0f,    111.0f,     90.0f,     27.0f,     25.0f,     10.0f,      7.4f,
     90526.0f,  16388.0f,  15711.0f,  13419.0f,   4007.0f,   3704.0f,   3185.0f,
      2696.0f,   2588.0f,    946.0f,    813.0f,    687.0f,    472.0f,    448.0f,
       170.0f,    167.0f,    165.0f,    125.0f,    101.0f,     34.0f,     32.0f,
        12.0f,      7.3f,  93105.0f,  16939.0f,  16244.0f,  13814.0f,   4161.0f,
      3852.0f,   3301.0f,   2802.0f,   2687.0f,    994.0f,    858.0f,    724.0f,
       503.0f,    478.0f,    193.0f,    187.0f,    181.0f,    139.0f,    112.0f,
        41.0f,     38.0f,     15.0f,      8.4f,  95730.0f,  17493.0f,  16785.0f,
     14214.0f,   4320.0f,   4005.0f,   3420.0f,   2910.0f,   2788.0f,   1044.0f,
       904.0f,    761.0f,    535.0f,    508.0f,    217.0f,    211.0f,    196.0f,
       153.0f,    123.0f,     48.0f,     44.0f,     19.0f,     11.0f,      9.3f,
     98404.0f,  18049.0f,  17337.0f,  14619.0f,   4483.0f,   4162.0f,   3452.0f,
      3109.0f,   2890.0f,   1096.0f,    951.0f,    798.0f,    567.0f,    538.0f,
       242.0f,    235.0f,    212.0f,    167.0f,    134.0f,     55.0f,     51.0f,
        24.0f,     14.0f,     10.7f, 101137.0f,  18639.0f,  17907.0f,  15031.0f,
      4652.0f,   4324.0f,   3666.0f,   3134.0f,   2998.0f,   1153.0f,   1003.0f,
       839.0f,    603.0f,    572.0f,    268.0f,    260.0f,    231.0f,    183.0f,
       147.0f,     65.0f,     61.0f,     33.0f,     19.0f,     14.0f,      4.0f,
    103922.0f,  19237.0f,  18484.0f,  15444.0f,   4822.0f,   4491.0f,   3793.0f,
      3254.0f,   3111.0f,   1214.0f,   1060.0f,    884.0f,    642.0f,    609.0f,
       296.0f,    287.0f,    253.0f,    201.0f,    161.0f,     77.0f,     73.0f,
        40.0f,     25.0f,     19.0f,      5.3f, 106755.0f,  19840.0f,  19083.0f,
     15871.0f,   5002.0f,   4656.0f,   3921.0f,   3374.0f,   3223.0f,   1274.0f,
      1116.0f,    928.0f,    680.0f,    645.0f,    322.0f,    313.0f,    274.0f,
       218.0f,    174.0f,     88.0f,     83.0f,     45.0f,     29.0f,     22.0f,
         6.3f,      5.7f, 109651.0f,  20472.0f,  19693.0f,  16300.0f,   5182.0f,
      4830.0f,   4049.0f,   3494.0f,   3335.0f,   1333.0f,   1171.0f,    970.0f,
       717.0f,    679.0f,    347.0f,    338.0f,    293.0f,    233.0f,    185.0f,
        97.0f,     91.0f,     50.0f,     33.0f,     25.0f,      6.0f,      6.0f,
    112601.0f,  21105.0f,  20314.0f,  16733.0f,   5367.0f,   5001.0f,   4178.0f,
      3613.0f,   3446.0f,   1390.0f,   1225.0f,   1011.0f,    752.0f,    712.0f,
       372.0f,    362.0f,    312.0f,    248.0f,    195.0f,    104.0f,     97.0f,
        50.0f,     32.0f,     24.0f,      6.0f,      6.0f,      6.0f, 115606.0f,
     21757.0f,  20948.0f,  17166.0f,   5548.0f,   5182.0f,   4308.0f,   3733.0f,
      3557.0f,   1446.0f,   1278.0f,   1050.0f,    785.0f,    743.0f,    396.0f,
       386.0f,    329.0f,    261.0f,    203.0f,    110.0f,    101.0f,     52.0f,
        34.0f,     24.0f,      6.1f,      6.0f,      6.0f, 118678.0f,  22426.0f,
     21600.0f,  17610.0f,   5723.0f,   5366.0f,   4440.0f,   3854.0f,   3669.0f,
      1504.0f,   1331.0f,   1089.0f,    819.0f,    774.0f,    421.0f,    410.0f,
       346.0f,    274.0f,    211.0f,    116.0f,    106.0f,     54.0f,     35.0f,
        25.0f,      6.0f,      6.0f,      6.0f, 121818.0f,  23097.0f,  22266.0f,
     18056.0f,   5933.0f,   5541.0f,   4557.0f,   3977.0f,   3783.0f,   1563.0f,
      1384.0f,   1128.0f,    853.0f,    805.0f,    446.0f,    434.0f,    356.0f,
       287.0f,    219.0f,    122.0f,    111.0f,     53.0f,     34.0f,     23.0f,
         6.0f,      6.0f, 125027.0f,  23773.0f,  22944.0f,  18504.0f,   6121.0f,
      5710.0f,   4667.0f,   4102.0f,   3898.0f,   1623.0f,   1439.0f,   1167.0f,
       887.0f,    836.0f,    467.0f,    452.0f,    355.0f,    301.0f,    220.0f,
       123.0f,    112.0f,     54.0f,     44.0f,     36.0f,      6.0f,      6.0f,
    128220.0f,  24460.0f,  23779.0f,  18930.0f,   6288.0f,   5895.0f,   4797.0f,
      4236.0f,   4014.0f,   1664.0f,   1493.0f,   1194.0f,    919.0f,    864.0f,
       494.0f,    479.0f,    384.0f,    314.0f,    239.0f,    126.0f,    119.0f,
        60.0f,     39.0f,     27.0f,     11.0f,      5.0f,      6.0f, 131590.0f,
     25275.0f,  24385.0f,  19452.0f,   6556.0f,   6147.0f,   4977.0f,   4366.0f,
      4133.0f,   1729.0f,   1554.0f,   1236.0f,    955.0f,    898.0f,    520.0f,
       504.0f,    401.0f,    329.0f,    248.0f,    142.0f,    124.0f,     63.0f,
        41.0f,     27.0f,     12.0f,      6.0f,      4.0f, 135960.0f,  26110.0f,
     25250.0f,  19930.0f,   6754.0f,   6359.0f,   5109.0f,   4492.0f,   4247.0f,
      1789.0f,   1610.0f,   1273.0f,    987.0f,    925.0f,    546.0f,    529.0f,
       412.0f,    338.0f,    251.0f,    142.0f,    129.0f,     61.0f,     39.0f,
        25.0f,      9.0f,      6.0f, 139490.0f,  26900.0f,  26020.0f,  20410.0f,
      6977.0f,   6754.0f,   5252.0f,   4630.0f,   4369.0f,   1857.0f,   1674.0f,
      1316.0f,   1024.0f,    959.0f,    573.0f,    554.0f,    429.0f,    353.0f,
       260.0f,    148.0f,    135.0f,     63.0f,     40.0f,     25.0f,      9.0f,
         6.0f, 143090.0f,  27700.0f,  26810.0f,  20900.0f,   7205.0f,   6793.0f,
      5397.0f,   4766.0f,   4498.0f,   1933.0f,   1746.0f,   1366.0f,   1068.0f,
      1000.0f,    606.0f,    587.0f,    453.0f,    375.0f,    275.0f,    160.0f,
       145.0f,     69.0f,     45.0f,     29.0f,     15.0f,      7.0f
};

/** Data structure for describing an atomic shell. */
struct atomic_shell {
        /** Oscillator strength. */
        double f;
        /** Binding energy. */
        double E;
};

/* Normalise electronic oscillators for given material properties
 *
 * Oscillators strength and level are scaled in order to match the Mean
 * Excitation Energy, I.
 */
static double atomic_shell_normalise(int n_shells, struct atomic_shell * shells,
    double ZoA, double I, double density)
{
        double ftot = 0., lnI = 0.;
        int i;
        struct atomic_shell * shell;
        for (i = 0, shell = shells; i < n_shells; i++, shell++) {
                ftot += shell->f;
                lnI += shell->f * log(shell->E);
        }
        ftot = 1. / ftot;
        lnI *= ftot;

        const double wp = 28.816 * sqrt(ZoA * density * 1E-03);
        const double aS = I * 1E+09 / exp(lnI);
        const double r = aS / wp;

        for (i = 0, shell = shells; i < n_shells; i++, shell++) {
                shell->f *= ftot;
                shell->E *= r;
        }

        return aS;
}

static int atomic_shell_getn1(double Z, int * i0)
{
        int iZ = (int)Z - 1;
        if (iZ >= 100) iZ = 99;
        const int i = atomic_shell_index[iZ];
        if (i0 != NULL) *i0 = i;
        return atomic_shell_index[iZ + 1] - i;
}

static int atomic_shell_getn(double Z, double A)
{
        int n;

        if ((Z == 11.) && (A == 22.)) {
                /* Use CaCO3 electronic structure for Standard Rock */
                n = atomic_shell_getn1(6, NULL);
                n += atomic_shell_getn1(8, NULL);
                n += atomic_shell_getn1(20, NULL);

        } else {
                n = atomic_shell_getn1(Z, NULL);
        }

        return n;
}

static int atomic_shell_copyweight1(
    double Z, struct atomic_shell * shells, double w)
{
        int i0;
        const int n = atomic_shell_getn1(Z, &i0);

        int i;
        for (i = 0; i < n; i++) {
                const int is = i + i0;
                shells[i].f = w * atomic_shell_occupancy[is];
                shells[i].E = atomic_shell_energy[is];
        }

        return n;
}

static int atomic_shell_copyweight(
    double Z, double A, struct atomic_shell * shells, double w)
{
        int n;
        if ((Z == 11.) && (A == 22.)) {
                /* Use CaCO3 electronic structure for Standard Rock */
                w *= 11. / (6. + 3 * 8. + 20.);
                n = atomic_shell_copyweight1(6, shells, w);
                n += atomic_shell_copyweight1(8, shells + n, 3 * w);
                n += atomic_shell_copyweight1(20, shells + n, w);
        } else {
                n = atomic_shell_copyweight1(Z, shells, w);
        }

        return n;
}

/* Unpack atomic shells for a collection of elements */
static struct atomic_shell * atomic_shell_unpack(int n_elements,
    const double * Z, const double * A, const double * w, double I,
    double density, double * ZoA, int * n_shells_ptr)
{
        int i, n_shells = 0;
        for (i = 0; i < n_elements; i++) {
                n_shells += atomic_shell_getn(Z[i], A[i]);
        }

        struct atomic_shell * shells = allocate(n_shells * sizeof(*shells));
        if (shells == NULL) return NULL;

        double Ztot = 0., Atot = 0.;
        int is = 0;
        for (i = 0; i < n_elements; i++) {
                const double wi = w[i] / A[i];
                Ztot += Z[i] * wi;
                Atot += w[i];
                is += atomic_shell_copyweight(Z[i], A[i], shells + is, wi);
        }

        *n_shells_ptr = n_shells;
        if (ZoA != NULL) *ZoA = Ztot / Atot;

        atomic_shell_normalise(is, shells, Ztot / Atot, I, density);

        return shells;
}

/* Build electronic oscillators for a material from atomic shells
 *
 * Oscillators strength and level are set from atomic binding energies of
 * individual atomic elements. A global scaling factor is applied in order to
 * match the Mean Excitation Energy, I.
 *
 * Reference:
 *     U. Fano, Ann. Rev. Nucl. Sci. 13, 1 (1963)
 *     D. Liljequist, J. Phys. D: Appl. Phys. 16 1567 (1983)
 */
struct atomic_shell * atomic_shell_create(
    const struct pumas_physics * physics, int material, int * n_shells_ptr,
    double * aS_ptr)
{
        int n_shells = 0;
        int i;
        for (i = 0; i < physics->elements_in[material]; i++) {
                const struct material_component * const c =
                    physics->composition[material] + i;
                const struct atomic_element * const e =
                    physics->element[c->element];
                n_shells += atomic_shell_getn(e->Z, e->A);
        }
        if (n_shells_ptr != NULL) *n_shells_ptr = n_shells;

        struct atomic_shell * shells = allocate(n_shells * sizeof(*shells));

        int is = 0;
        for (i = 0; i < physics->elements_in[material]; i++) {
                const struct material_component * const c =
                    physics->composition[material] + i;
                const struct atomic_element * const e =
                    physics->element[c->element];

                const double wi = c->fraction / e->A;
                is += atomic_shell_copyweight(e->Z, e->A, shells + is, wi);
        }

        const double aS = atomic_shell_normalise(n_shells, shells,
            physics->material_ZoA[material], physics->material_I[material],
            physics->material_density[material]);

        if (aS_ptr != NULL) * aS_ptr = aS;

        return shells;
}

/* The density effect for the electronic energy loss.
 *
 * The density effect is computed following Fano (1963). Oscillators strength
 * and level have been set from atomic binding energies of individual atomic
 * elements. A global scaling factor is applied in order to match the Mean
 * Excitation Energy (see `atomic_shell_create` above).
 *
 * Reference:
 *     U. Fano, Ann. Rev. Nucl. Sci. 13, 1 (1963)
 *     D. Liljequist, J. Phys. D: Appl. Phys. 16 1567 (1983)
 */
static double electronic_density_effect(
    int n_shells, struct atomic_shell * shells, double gamma)
{
        const double y = 1. / (gamma * gamma);
        double ymax = 0.;
        int i;
        for (i = 0; i < n_shells; i++) {
                const double ei = shells[i].E;
                ymax += shells[i].f / (ei * ei);
        }
        if (ymax <= y) return 0.;

        double l2min = 0., l2max = 1. / y, l2;
        for (;;) {
                l2 = 0.5 * (l2min + l2max);
                double yi = 0.;
                for (i = 0; i < n_shells; i++) {
                        const double ei = shells[i].E;
                        yi += shells[i].f / (ei * ei + l2);
                }

                if ((fabs(yi - y) <= DBL_EPSILON) ||
                    (l2max - l2min <= DBL_EPSILON)) {
                        break;
                } else if (yi > y) {
                        l2min = l2;
                } else {
                        l2max = l2;
                }
        }

        double delta = -l2 * y;
        for (i = 0; i < n_shells; i++) {
                const double ei = shells[i].E;
                delta += shells[i].f * log(1. + l2 / (ei * ei));
        }

        return delta;
}

/* The average energy loss from atomic electrons. */
static double electronic_energy_loss(double ZoA, double I, int n_shells,
    struct atomic_shell * shells,  double mass, double kinetic,
    double * delta_ptr)
{
        /* Kinematic factors. */
        const double E = kinetic + mass;
        const double P2 = kinetic * (kinetic + 2. * mass);
        const double beta2 = P2 / (E * E);
        const double gamma = E / mass;

        /* Electronic Bremsstrahlung correction. */
        const double r = ELECTRON_MASS / mass;
        const double Qmax =
            2. * r * P2 / (mass * (1. + r * r) + 2. * r * E);
        const double lQ = log(1. + 2. * Qmax / ELECTRON_MASS);
        const double Delta =
            ALPHA_EM / (2 * M_PI) * (log(2. * gamma) - lQ / 3.) * lQ * lQ;

        /* Density effect. */
        const double delta = electronic_density_effect(
            n_shells, shells, gamma);
        if (delta_ptr != NULL) * delta_ptr = delta;

        /* Modified Bethe-Bloch equation. */
        return 2 * M_PI * ELECTRON_RADIUS * ELECTRON_RADIUS * ELECTRON_MASS *
            AVOGADRO_NUMBER * ZoA / (beta2 * 1E-03) * (
            log(2. * ELECTRON_MASS * beta2 * gamma * gamma * Qmax / (I * I)) -
            2 * beta2 - delta + 0.25 * Qmax * Qmax / (E * E) + Delta);
}

double pumas_electronic_density_effect(int n_elements, const double * Z,
    const double * A, const double * w, double I, double density, double gamma)
{
        struct atomic_shell *  shells;
        int n_shells;
        double w1 = 1;
        if (w == NULL) w = &w1;
        shells = atomic_shell_unpack(
            n_elements, Z, A, w, I, density, NULL, &n_shells);
        if (shells == NULL) return -1.;

        const double d = electronic_density_effect(n_shells, shells, gamma);
        free(shells);

        return d;
}

double pumas_electronic_stopping_power(int n_elements, const double * Z,
    const double * A, const double * w, double I, double density, double mass,
    double energy)
{
        struct atomic_shell  * shells;
        int n_shells;
        double w1 = 1;
        if (w == NULL) w = &w1;
        double ZoA;
        shells = atomic_shell_unpack(
            n_elements, Z, A, w, I, density, &ZoA, &n_shells);
        if (shells == NULL) return -1.;

        const double d = electronic_energy_loss(
            ZoA, I, n_shells, shells, mass, energy, NULL);
        free(shells);

        return d;
}

/* Container for atomic element tabulation data */
struct tabulation_element {
        /* The API proxy */
        struct physics_element api;
        /* Placeholder for tabulation data */
        double data[];
};

static void tabulate_element(struct pumas_physics * physics,
    struct tabulation_element * data, int n_energies, double * kinetic)
{
        const struct atomic_element * element =
            physics->element[data->api.index];

        /* Loop over the kinetic energy values. */
        double * v;
        int ik, ip;
        for (ik = 0, v = data->data; ik < n_energies; ik++) {
                const double k = kinetic[ik];
                const double x = 1E-06;
                const int n =
                    (int)(-1E+02 * log10(x)); /* 100 pts per decade. */
                for (ip = 0; ip < N_DEL_PROCESSES - 1; ip++, v++)
                        *v = compute_dcs_integral(
                            physics, 1, element, k, dcs_get(ip), x, 1, n);
        }
}

/*
 * Create a new energy loss table for an element and add it to the stack of
 * temporary data.
 */
struct physics_element * tabulation_element_create(
    struct physics_tabulation_data * data, int element)
{
        /* Allocate memory for the new element. */
        struct physics_element * e =
            allocate(sizeof(struct tabulation_element) +
                3 * data->n_energies * sizeof(double));
        if (e == NULL) return NULL;
        e->index = element;
        e->fraction = 0.;

        /* Add the element's data on top of the stack. */
        if (data->elements != NULL) data->elements->next = e;
        e->prev = data->elements;
        e->next = NULL;
        data->elements = e;

        return e;
}

/*
 * Get the energy loss table for an element from the temporary data and
 * put it on top of the stack.
 */
struct physics_element * tabulation_element_get(
    struct physics_tabulation_data * data, int element)
{
        struct physics_element * e;
        for (e = data->elements; e != NULL; e = e->prev) {
                if (e->index == element) {
                        struct physics_element * next = e->next;
                        if (next != NULL) {
                                /* Put the element on top of the stack. */
                                struct physics_element * prev = e->prev;
                                if (prev != NULL) prev->next = next;
                                next->prev = prev;
                                data->elements->next = e;
                                e->prev = data->elements;
                                e->next = NULL;
                                data->elements = e;
                        }
                        return e;
                }
        }

        /* The element wasn't found, return `NULL`. */
        return NULL;
}

/**
 * Tabulate the energy loss for the given material and set of energies.
 *
 * @param physics    Handle for the Physics tables.
 * @param data       The tabulation settings.
 * @return On success `PUMAS_RETURN_SUCCESS` is returned otherwise an error
 * code is returned as detailed below.
 *
 * This function allows to generate an energy loss file for a given material and
 * a set of kinetic energy values. **Note** that the Physics must have been
 * initialised with the `_init` function in dry mode. The material atomic
 * composition is specified by the MDF provided at initialisation. Additional
 * Physical properties can be specified by filling the input *data* structure.
 *
 * __Warnings__
 *
 * This function is **not** thread safe.
 *
 * __Error codes__
 *
 *     PUMAS_RETURN_INDEX_ERROR     The material index is not valid.
 *
 *     PUMAS_RETURN_IO_ERROR        The output file already exists.
 *
 *     PUMAS_RETURN_MEMORY_ERROR    Some memory couldn't be allocated.
 *
 *     PUMAS_RETURN_PATH_ERROR      The output file could not be created.
 *
 */
enum pumas_return physics_tabulate(struct pumas_physics * physics,
    struct physics_tabulation_data * data, struct error_context * error_)
{
        /* Check the material index */
        const int material = data->material;
        if ((material < 0) ||
            (material >= physics->n_materials - physics->n_composites)) {
                return ERROR_VREGISTER(PUMAS_RETURN_INDEX_ERROR,
                    "invalid material index [%d]", material);
        }

        /* Set the energy grid */
        if ((data->n_energies <= 0) || (data->energy == NULL)) {
                static double energy_[201];
                data->energy = energy_;

                if (physics->particle == PUMAS_PARTICLE_MUON) {
                        /* For muons an extended PDG like energy grid is used
                         * by default.
                         */
                        energy_[0] = 1.000E-03;
                        energy_[1] = 1.200E-03;
                        energy_[2] = 1.400E-03;
                        energy_[3] = 1.700E-03;
                        energy_[4] = 2.000E-03;
                        energy_[5] = 2.500E-03;
                        energy_[6] = 3.000E-03;
                        energy_[7] = 3.500E-03;
                        energy_[8] = 4.000E-03;
                        energy_[9] = 4.500E-03;
                        energy_[10] = 5.000E-03;
                        energy_[11] = 5.500E-03;
                        energy_[12] = 6.000E-03;
                        energy_[13] = 7.000E-03;
                        energy_[14] = 8.000E-03;
                        energy_[15] = 9.000E-03;

                        const int n_per_decade = 16;
                        const int n_decades = 11;
                        data->n_energies = (n_decades + 1) * n_per_decade + 1;

                        int i;
                        for (i = 1; i <= n_decades; i++) {
                                int j;
                                for (j = 0; j < n_per_decade; j++) {
                                        energy_[i * n_per_decade + j] = 10 *
                                            energy_[(i - 1) * n_per_decade + j];
                                }
                        }
                        energy_[(n_decades + 1) * n_per_decade] = 10 *
                            energy_[n_decades * n_per_decade];
                } else {
                        /* For taus a logarithmic energy grid is used by
                         * default.
                         */
                        data->n_energies = 201;
                        double emin = 1E+02, emax = 1E+12;
                        const double dlnk = log(emax / emin) /
                            (data->n_energies - 1);
                        int i;
                        for (i = 0; i < data->n_energies; i++) {
                                energy_[i] = emin * exp(dlnk * i);
                        }
                }
        }

        /*
         * Tabulate the radiative energy losses for the constitutive atomic
         * elements, if not already done. Note that the element are also sorted
         * on top of the *data* stack.
         */
        struct material_component * component;
        int iel;
        for (iel = 0, component = physics->composition[material];
             iel < physics->elements_in[material]; iel++, component++) {
                /*
                 * Get the requested element's data and put them on top of
                 * the stack for further usage.
                 */
                struct physics_element * e =
                    tabulation_element_get(data, component->element);

                if (e == NULL) {
                        /* Create and tabulate the new element. */
                        e = tabulation_element_create(data, component->element);
                        if (e == NULL) return PUMAS_RETURN_MEMORY_ERROR;
                        tabulate_element(physics,
                            (struct tabulation_element *)e, data->n_energies,
                            data->energy);
                }

                /* Set the fraction in the current material. */
                e->fraction = component->fraction;
        }

        /* Check and open the output file. */
        int offset_dir, size_name;
        enum pumas_return rc;
        if ((rc = mdf_format_path(data->outdir, physics->mdf_path,
            &data->path, &offset_dir, &size_name, error_)) !=
            PUMAS_RETURN_SUCCESS) {
                return rc;
        } else {
                size_name += strlen(physics->dedx_filename[material]) + 1;
                char * new_name = reallocate(data->path, size_name);
                if (new_name == NULL) {
                        return ERROR_REGISTER_MEMORY();
                }
                data->path = new_name;
                strcpy(data->path + offset_dir,
                    physics->dedx_filename[material]);
        }

        FILE * stream;
        if (data->overwrite == 0) {
                /* Check if the file already exists. */
                stream = fopen(data->path, "r");
                if (stream != NULL) {
                        fclose(stream);
                        return PUMAS_RETURN_IO_ERROR;
                }
        }
        stream = fopen(data->path, "w+");
        if (stream == NULL) return PUMAS_RETURN_PATH_ERROR;

        /* Print the header. */
        const char * type =
            (physics->particle == PUMAS_PARTICLE_MUON) ? "Muon" : "Tau";
        fprintf(stream, " Incident particle is a %s with M = %.5lf MeV\n", type,
            (double)(physics->mass * 1E+03));
        fprintf(stream, " Index = %d: %s\n", material,
            physics->material_name[material]);
        fprintf(stream, "      Absorber with <Z/A> = %.5lf\n",
            physics->material_ZoA[material]);
        fputs(" Density effect computed following Fano (1963)\n", stream);
        fprintf(stream, " Models for Radloss: brems = %4s, pair = %4s, "
            "photonuc = %4s\n",
            physics->model_bremsstrahlung, physics->model_pair_production,
            physics->model_photonuclear);
        fprintf(stream, "\n *** Table generated with PUMAS v%d.%d ***\n\n",
            PUMAS_VERSION_MAJOR, PUMAS_VERSION_MINOR);
        fprintf(stream,
            "      T         p     Ionization  brems     pair     "
            "photonuc  Radloss    dE/dx   CSDA Range  delta   beta\n");
        fprintf(stream, "    [MeV]    [MeV/c]  -----------------------"
                        "[MeV cm^2/g]------------------------  [g/cm^2]\n");

        /* Build electronic shells by merging element's one */
        int n_shells = 0;
        struct atomic_shell * shells = atomic_shell_create(
            physics, material, &n_shells, NULL);

        /* Loop on the kinetic energy values and print the table. */
        const int n = data->n_energies + 1;
        if (data->work == NULL) {
                data->work = allocate(
                    n * (N_DEL_PROCESSES + 5) * sizeof(double));
        }
        double * elec = data->work;
        double * delta = elec + n;
        double * rads = delta + n;
        double * dedx = rads + (N_DEL_PROCESSES -1) * n;
        double * K = dedx + n;
        double * X = K + n;
        double * X_dK = X + n;

        K[0] = 0.;
        X[0] = 0.;

        int i;
        for (i = 0; i < data->n_energies; i++) {
                /* Compute the electronic energy loss. */
                delta[i] = 0.;
                elec[i] = electronic_energy_loss(
                    physics->material_ZoA[material],
                    physics->material_I[material], n_shells, shells,
                    physics->mass, data->energy[i], delta + i);

                /* Compute radiative losses */
                double * brad = rads + i * (N_DEL_PROCESSES - 1);
                memset(brad, 0x0, sizeof(double) * (N_DEL_PROCESSES - 1));
                struct tabulation_element * e;
                int iel;
                for (iel = 0, e = (struct tabulation_element *)data->elements;
                     iel < physics->elements_in[material];
                     iel++, e = (struct tabulation_element *)e->api.prev) {
                        int j;
                        for (j = 0; j < N_DEL_PROCESSES - 1; j++)
                                brad[j] += e->api.fraction *
                                    e->data[(N_DEL_PROCESSES - 1) * i + j];
                }

                /* Update the table. */
                const double radloss = brad[0] + brad[1] + brad[2];
                dedx[i] = radloss + elec[i];
                K[i + 1] = data->energy[i];
                X[i + 1] = 1. / dedx[i];
        }

        /* Integrate the grammage range */
        math_pchip_integrate(n, K, X, X_dK);

        /* Dump the energy loss table */
        for (i = 0; i < data->n_energies; i++) {
                const double p = sqrt(
                    data->energy[i] * (data->energy[i] + 2. * physics->mass));
                double * brad = rads + i * (N_DEL_PROCESSES - 1);
                const double radloss = brad[0] + brad[1] + brad[2];
                const double beta = p / (data->energy[i] + physics->mass);
                const double MeV = 1E+03;
                const double cmgs = 1E+04;
                fprintf(stream, "  %.3lE %.3lE %.3lE %.3lE %.3lE %.3lE "
                                "%.3lE %.3lE %.3lE %7.4lf %7.5lf\n",
                    data->energy[i] * MeV, p * MeV, elec[i] * cmgs,
                    brad[0] * cmgs, brad[1] * cmgs, brad[2] * cmgs,
                    radloss * cmgs, dedx[i] * cmgs, X[i + 1] * MeV / cmgs,
                    delta[i], beta);
        }

        /* Free, close and return. */
        free(shells);
        fclose(stream);
        return PUMAS_RETURN_SUCCESS;
}

/**
 * Clear the temporary memory used for the tabulation of materials.
 *
 * @param data    The tabulation data.
 *
 * This function allows to clear any temporary memory allocated by the
 * `pumas_physics_tabulate` function.
 *
 * __Warnings__
 *
 * This function is **not** thread safe.
 */
void physics_tabulation_clear(const struct pumas_physics * physics,
    struct physics_tabulation_data * data)
{
        deallocate(data->path);
        data->path = NULL;

        deallocate(data->work);
        data->work = NULL;

        struct physics_element * e;
        for (e = data->elements; e != NULL;) {
                struct physics_element * prev = e->prev;
                deallocate(e);
                e = prev;
        }
        data->elements = NULL;
}


/**
 * Get the physics differential Cross-Section (DCS) for a given process.
 */
enum pumas_return pumas_physics_dcs(
    const struct pumas_physics * physics, enum pumas_process process,
    const char ** model, pumas_dcs_t ** dcs)
{
        ERROR_INITIALISE(pumas_physics_dcs);

        if (physics == NULL) {
                return ERROR_NOT_INITIALISED();
        }

        if (process == PUMAS_PROCESS_BREMSSTRAHLUNG) {
                if (dcs != NULL) *dcs = physics->dcs_bremsstrahlung;
                if (model != NULL) *model = physics->model_bremsstrahlung;
        } else if (process == PUMAS_PROCESS_PAIR_PRODUCTION) {
                if (dcs != NULL) *dcs = physics->dcs_pair_production;
                if (model != NULL) *model = physics->model_pair_production;
        } else if (process == PUMAS_PROCESS_PHOTONUCLEAR) {
                if (dcs != NULL) *dcs = physics->dcs_photonuclear;
                if (model != NULL) *model = physics->model_photonuclear;
        } else {
                return ERROR_FORMAT(PUMAS_RETURN_INDEX_ERROR,
                    "bad process (expected a value in [0, 2], got %u)",
                    process);
        }

        return PUMAS_RETURN_SUCCESS;
}

/* Radiation logarithm calculated with Hartree-Fock model
 * Ref: Kelner, Kokoulin & Petrukhin (199), Physics of Atomic Nuclei, 62(11),
 *      1894-1898. doi:101134/1855464
 *
 * Values have been taken from Koehne et al.
 * (https://doi.org/10.1016/j.cpc.2013.04.001) since the original paper does not
 * seem to be available online.
 */
static double radiation_logarithm(double Z)
{
        const int i = (int)Z;
        if ((i >= 1) && (i <= 92)) {
                static double Lz[92] = {
                    202.4, 151.9, 159.9, 172.3, 177.9,
                    178.3, 176.6, 173.4, 170.0, 165.8,
                    165.8, 167.1, 169.1, 170.8, 172.2,
                    173.4, 174.3, 174.8, 175.1, 175.6,
                    176.2, 176.8,   0.0,   0.0,   0.0,
                    175.8,   0.0,   0.0, 173.1,   0.0,
                      0.0, 173.0,   0.0,   0.0, 173.5,
                      0.0,   0.0,   0.0,   0.0,   0.0,
                      0.0, 175.9,   0.0,   0.0,   0.0,
                      0.0,   0.0,   0.0,   0.0, 177.4,
                      0.0,   0.0, 178.6,   0.0,   0.0,
                      0.0,   0.0,   0.0,   0.0,   0.0,
                      0.0,   0.0,   0.0,   0.0,   0.0,
                      0.0,   0.0,   0.0,   0.0,   0.0,
                      0.0,   0.0,   0.0, 177.6,   0.0,
                      0.0,   0.0,   0.0,   0.0,   0.0,
                      0.0, 178.0,   0.0,   0.0,   0.0,
                      0.0,   0.0,   0.0,   0.0,   0.0,
                      0.0, 179.8
                };
                const double l = Lz[i - 1];
                if (l > 0) return l;
        }
        return 182.7;
}

/**
 * The Bremsstrahlung differential cross section according to
 * Kelner, Kokoulin & Petrukhin (KKP).
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param mu      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * The KKP Bremsstrahlung differential cross-section was initially implemented
 * following Groom et al. (see e.g.
 * http://pdg.lbl.gov/2020/AtomicNuclearProperties/adndt.pdf). Then, it has been
 * refined following Koehne et al. (https://doi.org/10.1016/j.cpc.2013.04.001)
 * by taking into account the nucleus excitation term (See e.g. Kelner et al.
 * https://cds.cern.ch/record/288828/files/MEPHI-024-95.pdf) and more accurate
 * radiation logarithm computations.
 */
static double dcs_bremsstrahlung_KKP(
    double Z, double A, double mu, double K, double q)
{
        /* Check inputs */
        if ((Z <= 0) || (A <= 0) || (mu <= 0) || (K <= 0) || (q <= 0))
                return 0.;

        const double Z13 = pow(Z, 1. / 3.);
        const double sqrte = 1.648721271;

        if (q >= K + mu * (1. - 0.75 * sqrte * Z13))
                return 0.;

        const double me = ELECTRON_MASS;
        const double phie_factor = mu / (me * me * sqrte);
        const double rem = 5.63588E-13 * me / mu;

        const double BZ_n = radiation_logarithm(Z) / Z13;
        const double BZ_e = (Z == 1.) ? 446. : 1429. / (Z13 * Z13);
        const double D_n = 1.54 * pow(A, 0.27);
        const double E = K + mu;
        const double dcs_factor = 7.297182E-07 * rem * rem * Z / E;

        const double delta_factor = 0.5 * mu * mu / E;
        const double qe_max = E / (1. + 0.5 * mu * mu / (me * E));

        const double nu = q / E;
        const double delta = delta_factor * nu / (1. - nu);
        const double muD_factor = mu + delta * (D_n * sqrte - 2.);
        double Phi_n, Phi_x, Phi_e;
        Phi_n = log(BZ_n * muD_factor /
            (D_n * (me + delta * sqrte * BZ_n)));
        if (Phi_n < 0.) Phi_n = 0.;
        if (Z >= 2) {
                Phi_x = log(mu * D_n / muD_factor);
                if (Phi_x < 0.) Phi_x = 0.;
        } else {
                Phi_x = 0.;
        }
        if (q < qe_max) {
                Phi_e = log(BZ_e * mu /
                    ((1. + delta * phie_factor) * (me + delta * sqrte * BZ_e)));
                if (Phi_e < 0.) Phi_e = 0.;
        } else
                Phi_e = 0.;

        const double dcs = dcs_factor *
            (Z * Phi_n + Phi_x + Phi_e) * (4. / 3. * (1. / nu - 1.) + nu);
        return (dcs < 0.) ? 0. : dcs;
}

/* Bremsstahlung DCS according to Andreev, Bezrukov & Bugaev.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param mu      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * Ref: https://arxiv.org/abs/hep-ph/0010322 (MUM)
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/crossection/parametrization/Bremsstrahlung.cxx
 */
static double dcs_bremsstrahlung_ABB(
    double Z, double A, double m, double K, double q)
{
#define SQRTE 1.648721270700128
#define ME    0.5109989461
#define RE    2.8179403227E-13
#define MMU   105.6583745

        /* Check inputs */
        if ((Z <= 0) || (A <= 0) || (m <= 0) || (K <= 0) || (q <= 0))
                return 0.;

        const double Z13 = pow(Z, 1. / 3.);

        if (q >= K + m * (1. - 0.75 * SQRTE * Z13))
                return 0.;

        /* Convert from GeV to MeV */
        const double energy = (K + m) * 1E+03;
        const double v = q * 1E+03 / energy;
        m *= 1E+03;

        /* Least momentum transferred to the nucleus (eq. 2.2) */
        const double Z3 = 1. / Z13;
        const double a1 = 184.15 * Z3 / (SQRTE * ME);    /* eq 2.18 */
        const double a2 = 1194 * Z3 * Z3 / (SQRTE * ME); /* eq.2.19 */

        /* Calculating the contribution of elastic nuclear and atomic form
         * factors (eq. 2.30)
         */
        const double qc   = 1.9 * MMU * Z3;
        double aux        = 2 * m / qc;
        const double zeta = sqrt(1 + aux * aux);

        const double delta = m * m * v / (2 * energy * (1 - v));
        const double x1    = a1 * delta;
        const double x2    = a2 * delta;

        double aux1, aux2, d1, d2, psi1, psi2;

        if (Z == 1) {
                d1 = 0;
                d2 = 0;
        } else {
                aux1 = log(m / qc);
                aux2 = 0.5 * zeta * log((zeta + 1) / (zeta - 1));
                d1   = aux1 + aux2;
                d2   = aux1 + 0.5 * ((3 - zeta * zeta) * aux2 + aux * aux);
        }

        /* eq. 2.20 and 2.21 */
        aux  = m * a1;
        aux1 = log(aux * aux / (1 + x1 * x1));
        aux  = m * a2;
        aux2 = log(aux * aux / (1 + x2 * x2));
        psi1 = 0.5 * ((1 + aux1) + (1 + aux2) / Z);
        psi2 = 0.5 * ((2. / 3 + aux1) + (2. / 3 + aux2) / Z);

        aux1 = x1 * atan(1 / x1);
        aux2 = x2 * atan(1 / x2);
        psi1 -= aux1 + aux2 / Z;
        aux = x1 * x1;
        psi2 += 2 * aux * (1 - aux1 + 0.75 * log(aux / (1 + aux)));
        aux = x2 * x2;
        psi2 += 2 * aux * (1 - aux2 + 0.75 * log(aux / (1 + aux))) / Z;

        psi1 -= d1;
        psi2 -= d2;
        const double result = (2 - 2 * v + v * v) * psi1 -
            (2. / 3) * (1 - v) * psi2;

        if (result < 0) return 0;

        aux = 2 * (ME / m) * RE * Z;
        return aux * aux * (ALPHA_EM / q) * result * 1E-04;
}

/* Bremsstahlung DCS according to Sandrock, Soedingrekso & Rhode.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param mu      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * Ref: https://arxiv.org/abs/1910.07050
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/crossection/parametrization/Bremsstrahlung.cxx
 */
static double dcs_bremsstrahlung_SSR(
    double Z, double A, double m, double K, double q)
{
        /* Check inputs */
        if ((Z <= 0) || (A <= 0) || (m <= 0) || (K <= 0) || (q <= 0))
                return 0.;

        const double z13 = pow(Z, 1. / 3.);
        if (q >= K + m * (1. - 0.75 * SQRTE * z13))
                return 0.;

        const double a[3] = {
            -0.00349, 148.84, -987.531};
        const double b[4] = {
            0.1642, 132.573, -585.361, 1407.77};
        const double c[6] = {
            -2.8922, -19.0156, 57.698, -63.418, 14.1166, 1.84206};
        const double d[6] = {
            2134.19, 581.823, -2708.85, 4767.05, 1.52918, 0.361933};

        /* Convert from GeV to MeV */
        const double energy = (K + m) * 1E+03;
        const double v = q * 1E+03 / energy;
        m *= 1E+03;

        const double Z13 = 1. / z13;
        const double rad_log = radiation_logarithm(Z);
        const double rad_log_inel = (Z == 1.) ? 446. : 1429.;
        const double Dn = 1.54 * pow(A, 0.27);

        const double mu_qc = m / (MMU * exp(1.) / Dn);
        const double rho = sqrt(1. + 4. * mu_qc * mu_qc);

        const double log_rho = log((rho + 1.) / (rho - 1.));
        const double delta1 = log(mu_qc) + 0.5 * rho * log_rho;
        const double delta2 = log(mu_qc) +
            0.25 * (3. * rho - rho * rho * rho) * log_rho + 2. * mu_qc * mu_qc;

        /* Least momentum transferred to the nucleus (eq. 7) */
        const double delta = m * m * v / (2. * energy * (1. - v));

        double phi1 = log(rad_log * Z13 * (m / ME) /
            (1. + rad_log * Z13 * exp(0.5) * delta / ME));
        double phi2 = log(rad_log * Z13 * exp(-1. / 6.) * (m / ME) /
            (1. + rad_log * Z13 * exp(1. / 3.) * delta / ME));
        phi1 -= delta1 * (1. - 1. / Z);
        phi2 -= delta2 * (1. - 1. / Z);

        /* s_atomic */
        const double s_atomic_1 = log(m / delta /
            (m * delta / (ME * ME) + SQRTE));
        const double s_atomic_2 = log(1. + ME /
            (delta * rad_log_inel * Z13 * Z13 * SQRTE));
        const double s_atomic = (4. / 3. * (1. - v) + v * v) *
            (s_atomic_1 - s_atomic_2);

        /* s_rad */
        double s_rad;

        if (v < .0 || v > 1.) {
                s_rad = 0.;
        } else if (v < 0.02) {
                s_rad = a[0] + a[1] * v + a[2] * v * v;
        } else if (v >= 0.02 && v < 0.1) {
                s_rad = b[0] + b[1] * v + b[2] * v * v + b[3] * v * v * v;
        } else if (v >= 0.01 && v < 0.9) {
                s_rad = c[0] + c[1] * v + c[2] * v * v;

                const double tmp = log(1. - v);
                s_rad += c[3] * v * log(v) + c[4] * tmp + c[5] * tmp * tmp;
        } else {
                s_rad = d[0] + d[1] * v + d[2] * v * v;

                const double tmp = log(1. - v);
                s_rad += d[3] * v * log(v) + d[4] * tmp + d[5] * tmp * tmp;
        }

        const double result = ((2. - 2. * v + v * v) * phi1 - 2. / 3. *
            (1. - v) * phi2) + 1. / Z * s_atomic +
            0.25 * ALPHA_EM * phi1 * s_rad;

        if (result <= 0.) return 0.;

        const double aux = 2 * (ME / m) * RE * Z;
        return aux * aux * (ALPHA_EM / q) * result * 1E-04;

#undef SQRTE
#undef ME
#undef RE
#undef MMU
}

/* Cutoff for Tsai's Bremsstahlung DDCS. */
static double dcs_bremsstrahlung_tsai_d2_cutoff(
    double Z, double A, double m, double K, double nu, double * mu0_p,
    double * muni_p)
{
        const double E = K + m;
        const double tmp = 0.5 * m * nu / (E * (E - nu));
        const double mu0 = tmp * tmp;
        const double muc = sqrt(mu0) - mu0;

        double mumax = 0., muni = 0.;
        if (muc > 0) {
                const double RN = data_nuclear_radius(Z, A);
                const double tmp2 = m * RN / HBAR_C;
                muni = tmp2 * tmp2 / 6.;

                mumax = mu0 * pow(mu0 * muni * 1E-05, -0.25) - mu0;
                /* Above this value the nuclear screening is close to zero and
                 * results are numericaly instable using double precision.
                 */
                if (muc < mumax) mumax = muc;
        }

        if (mu0_p != NULL) *mu0_p = mu0;
        if (muni_p != NULL) *muni_p = muni;

        return (mumax >= 0.) ? mumax : 0.;
}

/* Bremsstahlung DDCS according to Tsai.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param mu      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param nu      The kinetic energy lost to the photon.
 * @param mu      The projectile angular parameter.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * The projectile mu is computed from the photon scattering angle assuming
 * zero recoil for the target. Thus: mu = nu^2 / (K - nu)^2 mu_k.
 *
 * References:
 *   Y. Tsai, Rev. Mod. Phys. (1974).
 */
static double dcs_bremsstrahlung_tsai_d2(
    double Z, double A, double m, double K, double nu, double mu)
{
        double mu0, muni;
        const double mumax =
            dcs_bremsstrahlung_tsai_d2_cutoff(Z, A, m, K, nu, &mu0, &muni);
        if (mu >= mumax) return 0.;

        const double E = K + m;
        const double y = nu / E;
        const double c1 = (2 * (1. - y) + y * y) * mu0;
        const double c2 = 4 * (1. - y) * mu0 * mu0;
        const double mus = mu0 + mu;
        const double mu2 = mus * mus;
        const double d1 = 1 / mu2;
        const double d2 = d1 * d1;
        double ddcs = c1 * d1 - mu * c2 * d2;

        const double l2 = mu2 / (mu0 * mu0);
        const double q = muni * l2;
        const double r = mu0 * l2;
        const double qr = q * r;
        const double X = Z * Z * ((1. + 2 * qr) * log((1. + qr) / (r + qr)) -
            (1. - r) * (1 + 2 * q) / (1 + q));

        ddcs *= 2 * ALPHA_EM * ELECTRON_RADIUS * ELECTRON_RADIUS * X / nu;

        return ddcs;
}

/* Integrand for transport integral using Tsai ddcs */
static double dcs_bremsstrahlung_tsai_integrand(double Z, double A, double m,
    double kinetic, double nu, pumas_dcs_t * dcs)
{
#define N_GQ 12

        const double dcs_ref = dcs(Z, A, m, kinetic, nu);
        if (dcs_ref <= 0) return 0.;

        double mu0;
        double mumax = dcs_bremsstrahlung_tsai_d2_cutoff(
            Z, A, m, kinetic, nu, &mu0, NULL);
        if (mumax <= 0) return 0.;

        const double mumin = 1E-03 * mu0;

        const double mumax0 = 1E+03 * mu0; /* Upper bound for DCS */
        double y1 = 0., y0 = 0.;

        int k;
        for (k = 0; k < 2; k++) {
                int j;
                double xj[N_GQ], wj[N_GQ];
                math_gauss_quad_initialise(
                    N_GQ, mumin, mumax, 1, xj, wj);

                for (j = 0; j < N_GQ; j++) {
                        const double mu = xj[j];
                        const double ddcs = dcs_bremsstrahlung_tsai_d2(
                            Z, A, m, kinetic, nu, mu);
                        if (ddcs <= 0) continue;

                        if (k == 0) {
                                y1 += mu * ddcs * wj[j];
                                if (mumax0 >= mumax) {
                                        y0 += ddcs * wj[j];
                                }
                        } else {
                                y0 += ddcs * wj[j];
                        }
                }

                if (mumax0 >= mumax) {
                        break;
                } else {
                        mumax = mumax0;
                }
        }

        return (y0 > 0.) ? y1 * dcs_ref / y0 : 0.;

#undef N_GQ
}

/**
 * Integrate bremsstrahlung or pair production transport.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param ml      The projectile rest mass, in GeV
 * @param kinetic The projectile initial kinetic energy.
 * @param qlow    The lower cut on the kinetic energy lost to the photon / pair.
 * @param qhigh   The upper cut on the kinetic energy lost to the photon / pair.
 * @param dcs     The physics true DCS.
 * @return The corresponding value of the transport DCS, in m^2 / kg.
 */
static double dcs_bremsstrahlung_transport_integrate(double Z, double A,
    double m, double kinetic, double qlow, double qhigh, pumas_dcs_t * dcs)
{
        /* Set the integration range */
        const double Z13 = pow(Z, 1. / 3.);
        const double sqrte = 1.648721271;
        double qmax = kinetic + m * (1. - 0.75 * sqrte * Z13);
        if (qhigh < qmax) qmax = qhigh;

        double qmin = 1E-03 * qmax;
        if (qlow > qmin) qmin = qlow;

        if ((qmin >= qmax) || (qmin <= 0)) return 0.;

        int nint = 180;
        double qmid;
        if (qhigh >= kinetic) {
                qmid = 0.5 * qmax;
                nint /= 2;
        } else {
                qmid = qmax;
        }

        /* Integrate over the recoil energy using a logarithmic sampling. */
        double dcsint = 0.;
        double x0 = log(qmin), x1 = log(qmid);
        math_gauss_quad(nint, &x0, &x1); /* Initialisation. */

        double xi, wi;
        while (math_gauss_quad(0, &xi, &wi) == 0) { /* Iterations. */
                const double nu = exp(xi);
                const double y = dcs_bremsstrahlung_tsai_integrand(
                    Z, A, m, kinetic, nu, dcs);

                if (y > 0.) {
                        dcsint += y * nu * wi;
                }
        }

        if (qmid < qmax) {
                /* Integrate the upper part reverse log-scale due to
                 * divergences
                 */
                x0 = log(1E-12 * qmax);
                x1 = log(qmax - qmid);
                math_gauss_quad(nint, &x0, &x1); /* Initialisation. */

                while (math_gauss_quad(0, &xi, &wi) == 0) { /* Iterations. */
                        const double nu = qmax - exp(xi);
                        const double y = dcs_bremsstrahlung_tsai_integrand(
                            Z, A, m, kinetic, nu, dcs);

                        if (y > 0.) {
                                dcsint += y * (qmax - nu) * wi;
                        }
                }
        }

        return 2 * AVOGADRO_NUMBER / (A * 1E-03) * dcsint;

#undef N_GQ
}

/**
 * Wrapper for bremsstrahlung transport integral.
 */
double dcs_bremsstrahlung_transport(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double cutoff)
{
        return dcs_bremsstrahlung_transport_integrate(element->Z, element->A,
            physics->mass, K, 0., K * cutoff, physics->dcs_bremsstrahlung);
}

/**
 * Utility function for getting the kinematic range for pair production
 *
 * @param Z    The charge number of the target atom.
 * @param m    The projectile rest mass.
 * @param K    The projectile kinetic energy.
 * @param qmin The min kinetic energy lost to the photon.
 * @param qmax The max kinetic energy lost to the photon.
 */
void dcs_pair_production_range(
    double Z, double m, double K, double * qmin, double * qmax)
{
        if (qmax != NULL) {
                const double sqrte = 1.6487212707;
                const double Z13 = pow(Z, 1. / 3.);
                *qmax = K + m * (1. - 0.75 * sqrte * Z13);
        }
        if (qmin != NULL) *qmin = 4. * ELECTRON_MASS;
}

/**
 * The e+e- pair production differential cross section according to Kelner,
 * Kokoulin & Petrukhin.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param mu      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * Geant4 like implementation, see e.g. section 11.3.1 of the Geant4 Physics
 * Reference Manual.
 */
static double dcs_pair_production_KKP(
    double Z, double A_, double mass, double K, double q)
{
        if ((Z <= 0) || (A_ <= 0) || (mass <= 0) || (K <= 0) || (q <= 0))
                return 0.;

        /*  Check the bounds of the energy transfer. */
        double qmin, qmax;
        dcs_pair_production_range(Z, mass, K, &qmin, &qmax);
        if ((q < qmin) || (q > qmax)) return 0.;

        /*  Precompute some constant factors for the integration. */
        const double nu = q / (K + mass);
        const double r = mass / ELECTRON_MASS;
        const double beta = 0.5 * nu * nu / (1. - nu);
        const double xi_factor = 0.5 * r * r * beta;
        const double A = radiation_logarithm(Z);
        const double Z13 = pow(Z, 1. / 3.);
        const double AZ13 = A / Z13;
        const double sqrte = 1.6487212707;
        const double cL = 2. * sqrte * ELECTRON_MASS * AZ13;
        const double cLe = 2.25 * Z13 * Z13 / (r * r);

        /*  Compute the bound for the integral. */
        const double gamma = 1. + K / mass;
        const double x0 = 4. * ELECTRON_MASS / q;
        const double x1 = 6. / (gamma * (gamma - q / mass));
        const double argmin =
            (x0 + 2. * (1. - x0) * x1) / (1. + (1. - x1) * sqrt(1. - x0));
        if ((argmin >= 1.) || (argmin <= 0.)) return 0.;
        const double tmin = log(argmin);

        /*  Compute the integral over t = ln(1-rho). */
#define N_GQ 12
        const double * xGQ, * wGQ;
        math_gauss_quad_coefficients(N_GQ, &xGQ, &wGQ);

        double I = 0.;
        int i;
        for (i = 0; i < N_GQ; i++) {
                const double eps = exp(xGQ[i] * tmin);
                const double rho = 1. - eps;
                const double rho2 = rho * rho;
                const double rho21 = eps * (2. - eps);
                const double xi = xi_factor * rho21;
                const double xi_i = 1. / xi;

                /* Compute the e-term. */
                double Be;
                if (xi >= 1E+03)
                        Be =
                            0.5 * xi_i * ((3 - rho2) + 2. * beta * (1. + rho2));
                else
                        Be = ((2. + rho2) * (1. + beta) + xi * (3. + rho2)) *
                                log(1. + xi_i) +
                            (rho21 - beta) / (1. + xi) - 3. - rho2;
                const double Ye = (5. - rho2 + 4. * beta * (1. + rho2)) /
                    (2. * (1. + 3. * beta) * log(3. + xi_i) - rho2 -
                                      2. * beta * (2. - rho2));
                const double xe = (1. + xi) * (1. + Ye);
                const double cLi = cL / rho21;
                const double Le = log(AZ13 * sqrt(xe) * q / (q + cLi * xe)) -
                    0.5 * log(1. + cLe * xe);
                double Phi_e = Be * Le;
                if (Phi_e < 0.) Phi_e = 0.;

                /* Compute the mu-term. */
                double Bmu;
                if (xi <= 1E-03)
                        Bmu = 0.5 * xi * (5. - rho2 + beta * (3. + rho2));
                else
                        Bmu = ((1. + rho2) * (1. + 1.5 * beta) -
                                  xi_i * (1. + 2. * beta) * rho21) *
                                log(1. + xi) +
                            xi * (rho21 - beta) / (1. + xi) +
                            (1. + 2. * beta) * rho21;
                const double Ymu = (4. + rho2 + 3. * beta * (1. + rho2)) /
                    ((1. + rho2) * (1.5 + 2. * beta) * log(3. + xi) + 1. -
                                       1.5 * rho2);
                const double xmu = (1. + xi) * (1. + Ymu);
                const double Lmu =
                    log(r * AZ13 * q / (1.5 * Z13 * (q + cLi * xmu)));
                double Phi_mu = Bmu * Lmu;
                if (Phi_mu < 0.) Phi_mu = 0.;

                /* Update the t-integral. */
                I -= (Phi_e + Phi_mu / (r * r)) * (1. - rho) * wGQ[i] * tmin;
        }

        /* Atomic electrons form factor. */
        double zeta;
        if (gamma <= 35.)
                zeta = 0.;
        else {
                double gamma1, gamma2;
                if (Z == 1.) {
                        gamma1 = 4.4E-05;
                        gamma2 = 4.8E-05;
                } else {
                        gamma1 = 1.95E-05;
                        gamma2 = 5.30E-05;
                }
                zeta = 0.073 * log(gamma / (1. + gamma1 * gamma * Z13 * Z13)) -
                    0.26;
                if (zeta <= 0.)
                        zeta = 0.;
                else {
                        zeta /=
                            0.058 * log(gamma / (1. + gamma2 * gamma * Z13)) -
                            0.14;
                }
        }

        /* Gather the results and return the macroscopic DCS. */
        const double E = K + mass;
        const double dcs = 1.794664E-34 * Z * (Z + zeta) * (E - q) * I /
            (q * E);
        return (dcs < 0.) ? 0. : dcs;

#undef N_GQ
}

/**
 * The e+e- pair production doubly differential cross section according to
 * Sandrock, Soedingrekso & Rhode.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param mu      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @param rho     The e+e- asymmetry.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * Ref: https://arxiv.org/abs/1910.07050
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/crossection/parametrization/EPairProduction.cxx
 */
static inline double dcs_pair_production_d2_SSR(
    double Z, double A, double m, double K, double q, double rho)
{
#define SQRTE 1.648721270700128
#define ME    0.5109989461
#define RE    2.8179403227E-13

        if ((Z <= 0) || (A <= 0) || (m <= 0) || (K <= 0) || (q <= 0))
                return 0.;

        /*  Check the bounds of the energy transfer. */
        double qmin, qmax;
        dcs_pair_production_range(Z, m, K, &qmin, &qmax);
        if ((q < qmin) || (q > qmax)) return 0.;

        /* Change units and variables */
        const double energy = (K + m) * 1E+03;
        const double v = q / (K + m);
        m *= 1E+03;
        const double rad_log = radiation_logarithm(Z);

        const double const_prefactor = 4. / (3. * M_PI) * Z *
            pow(ALPHA_EM * RE, 2.);
        const double Z13 = pow(Z, -1. / 3.);
        const double d_n = 1.54 * pow(A, 0.27);

        rho = 1 - rho;
        const double rho2 = rho * rho;

        /* Zeta */
        double g1, g2;
        if (Z == 1.) {
                g1 = 4.4E-05;
                g2 = 4.8E-05;
        } else {
                g1 = 1.95E-05;
                g2 = 5.3E-05;
        }

        const double zeta1 = (0.073 * log(energy / m /
            (1. + g1 * pow(Z, 2. / 3.) * energy / m)) - 0.26);
        const double zeta2 = (0.058 * log(energy / m /
            (1 + g2 / Z13 * energy / m)) - 0.14);

        double zeta;
        if ((zeta1 > 0.) && (zeta2 > 0.)) {
                zeta = zeta1 / zeta2;
        } else {
                zeta = 0.;
        }

        const double beta = v * v / (2. * (1. - v));
        const double xi = pow(m * v / (2. * ME), 2.) * (1. - rho2) / (1. - v);

        /* Diagram e */
        const double Be = ((2. + rho2) * (1. + beta) +
            xi * (3. + rho2)) * log(1. + 1. / xi) +
            (1. - rho2 - beta) / (1. + xi) - (3. + rho2);

        const double Ce2 = ((1. - rho2) * (1. + beta) +
            xi * (3. - rho2)) * log(1. + 1. / xi) +
            2. * (1. - beta - rho2) / (1. + xi) - (3. - rho2);
        const double Ce1 = Be - Ce2;

        const double De = ((2. + rho2) * (1. + beta) +
            xi * (3. + rho2)) * math_dilog(1. / (1. + xi)) -
            (2. + rho2) * xi * log(1. + 1. / xi) -
            (xi + rho2 + beta) / (1. + xi);

        double Le1, Le2;
        if (De / Be > 0.) {
                const double tmp = De / Be;
                const double Xe = (tmp < 100) ? exp(-tmp) : 0.;
                Le1 = log(rad_log * Z13 * sqrt(1. + xi) /
                    (Xe + 2. * ME * exp(0.5) * rad_log * Z13 * (1. + xi) /
                    (energy * v * (1. - rho2)))) - tmp -
                    0.5 * log(Xe + pow(ME / m * d_n, 2.) * (1. + xi));

                Le2 = log(rad_log * Z13 * exp(-1. / 6.) * sqrt(1 + xi) /
                    (Xe + 2. * ME * exp(1. / 3.) * rad_log * Z13 * (1. + xi) /
                    (energy * v * (1. - rho2)))) - tmp -
                    0.5 * log(Xe + pow(ME / m * d_n, 2.) *
                    exp(-1. / 3.) * (1. + xi));
        } else {
                const double tmp = De / Be;
                const double Xe_inv = (tmp > -100) ? exp(De / Be) : 0;
                Le1 = log(rad_log * Z13 * sqrt(1. + xi) /
                    (1. + Xe_inv * 2. * ME * exp(0.5) * rad_log * Z13 *
                    (1. + xi) / (energy * v * (1. - rho2)))) - 0.5 * De / Be -
                    0.5 * log(1. + Xe_inv * pow(ME / m * d_n, 2.) * (1. + xi));

                Le2 = log(rad_log * Z13 * exp(-1. / 6.) * sqrt(1 + xi) /
                    (1. + Xe_inv * 2. * ME * exp(1. / 3.) * rad_log * Z13 *
                    (1. + xi) / (energy * v * (1. - rho2)))) - 0.5 * De / Be -
                    0.5 * log(1. + Xe_inv * pow(ME / m * d_n, 2.) *
                    exp(-1. / 3.) * (1. + xi));
        }

        double diagram_e = const_prefactor * (Z + zeta) * (1. - v) / v *
            (Ce1 * Le1 + Ce2 * Le2);
        if (diagram_e < 0.) diagram_e = 0.;

        /* Diagram mu */
        const double Bm = ((1. + rho2) * (1. + (3. * beta) / 2) - 1. / xi *
            (1. + 2. * beta) * (1. - rho2)) * log(1. + xi) +
            xi * (1. - rho2 - beta) / (1. + xi) +
            (1. + 2. * beta) * (1. - rho2);

        const double Cm2 = ((1. - beta) * (1. - rho2) -
            xi * (1. + rho2)) * log(1. + xi) / xi -
            2. * (1. - beta - rho2) / (1. + xi) +
            1. - beta - (1. + beta) * rho2;
        const double Cm1 = Bm - Cm2;

        const double Dm = ((1. + rho2) * (1. + (3. * beta) / 2.) -
            1. / xi * (1. + 2. * beta) * (1. - rho2)) *
            math_dilog(xi / (1. + xi)) + (1. + (3. * beta) / 2.) *
            (1. - rho2) / xi * log(1. + xi) + (1. - rho2 - beta / 2. *
            (1. + rho2) + (1. - rho2) / (2. * xi) * beta) * xi / (1. + xi);

        double Lm1, Lm2;
        if (Dm / Bm > 0.) {
                const double tmp = Dm / Bm;
                const double Xm = (tmp < 100) ? exp(-tmp) : 0.;
                Lm1 = log(m / ME * rad_log * Z13 / d_n /
                    (Xm + 2. * ME * exp(0.5) * rad_log * Z13 * (1. + xi) /
                    (energy * v * (1. - rho2)))) - tmp;
                Lm2 = log(m / ME * rad_log * Z13 / d_n /
                    (Xm + 2. * ME * exp(1. / 3.) * rad_log * Z13 * (1. + xi) /
                    (energy * v * (1. - rho2)))) - tmp;
        } else {
                const double tmp = Dm / Bm;
                const double Xmv = (tmp > -100) ? exp(tmp) : 0.;
                Lm1 = log(m / ME * rad_log * Z13 / d_n /
                    (1. + 2. * ME * exp(0.5) * rad_log * Z13 * (1. + xi) /
                    (energy * v * (1. - rho2)) * Xmv));
                Lm2 = log(m / ME * rad_log * Z13 / d_n /
                    (1. + 2. * ME * exp(1. / 3.) * rad_log * Z13 * (1. + xi) /
                    (energy * v * (1. - rho2)) * Xmv));
        }

        double diagram_mu = const_prefactor * (Z + zeta) * (1. - v) / v *
            pow(ME / m, 2.) * (Cm1 * Lm1 + Cm2 * Lm2);
        if (diagram_mu < 0.) diagram_mu = 0.;

        return (diagram_e + diagram_mu) * 1E-01 / energy;

#undef SQRTE
#undef ME
#undef RE
}

/**
 * The e+e- pair production differential cross section according to Sandrock,
 * Soedingrekso & Rhode.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param mu      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * Mixed implementation. The DDCS of PROPOSAL is used but the numeric
 * integration is done with a Gaussian quadrature a la Geant4.
 */
static double dcs_pair_production_SSR(
    double Z, double A_, double mass, double K, double q)
{
        if ((Z <= 0) || (A_ <= 0) || (mass <= 0) || (K <= 0) || (q <= 0))
                return 0.;

        /*  Check the bounds of the energy transfer. */
        if (q <= 4. * ELECTRON_MASS) return 0.;
        const double sqrte = 1.6487212707;
        const double Z13 = pow(Z, 1. / 3.);
        if (q >= K + mass * (1. - 0.75 * sqrte * Z13)) return 0.;

        /* Compute the bound for the integral */
        const double gamma = 1. + K / mass;
        const double x0 = 1. - 4. * ELECTRON_MASS / q;
        const double x1 = 1. - 6. / (gamma * (gamma - q / mass));

        double rmax;
        if ((x0 > 0) && (x1 > 0)) {
                rmax = sqrt(x0) * x1;
        } else {
                return 0.;
        }

        const double ri = 1. - rmax;
        if ((ri <= 0.) || (ri >= 1.)) return 0.;
        const double tmin = log(ri);

        /*  Compute the integral over t = ln(rho) */
#define N_GQ 12
        const double * xGQ, * wGQ;
        math_gauss_quad_coefficients(N_GQ, &xGQ, &wGQ);

        double I = 0.;
        int i;
        for (i = 0; i < N_GQ; i++) {
                const double rho = exp(xGQ[i] * tmin);
                I -= dcs_pair_production_d2_SSR(Z, A_, mass, K, q, rho) *
                    rho * wGQ[i] * tmin;
        }

        return (I < 0) ? 0. : I;

#undef N_GQ
}

/**
 * Wrapper for pair_production transport integral.
 */
double dcs_pair_production_transport(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double cutoff)
{
        const double qmin = 4 * ELECTRON_MASS;
        return dcs_bremsstrahlung_transport_integrate(element->Z, element->A,
            physics->mass, K, qmin, K * cutoff, physics->dcs_pair_production);
}

/** ALLM97 parameterisation of the proton structure function, F2.
 *
 * @param x       The Bjorken x parameter.
 * @param Q2      The negative four momentum squared.
 * @return The corresponding value of the proton structure function, F2.
 *
 * References:
 *      DESY 97-251 [arXiv:hep-ph/9712415].
 */
static inline double dcs_photonuclear_f2p_ALLM97(double x, double Q2)
{
        const double m02 = 0.31985;
        const double mP2 = 49.457;
        const double mR2 = 0.15052;
        const double Q02 = 0.52544;
        const double Lambda2 = 0.06527;

        const double cP1 = 0.28067;
        const double cP2 = 0.22291;
        const double cP3 = 2.1979;
        const double aP1 = -0.0808;
        const double aP2 = -0.44812;
        const double aP3 = 1.1709;
        const double bP1 = 0.36292;
        const double bP2 = 1.8917;
        const double bP3 = 1.8439;

        const double cR1 = 0.80107;
        const double cR2 = 0.97307;
        const double cR3 = 3.4942;
        const double aR1 = 0.58400;
        const double aR2 = 0.37888;
        const double aR3 = 2.6063;
        const double bR1 = 0.01147;
        const double bR2 = 3.7582;
        const double bR3 = 0.49338;

        const double M = 0.5 * (PROTON_MASS + NEUTRON_MASS);
        const double M2 = M * M;
        const double W2 = M2 + Q2 * (1.0 / x - 1.0);
        const double t = log(log((Q2 + Q02) / Lambda2) / log(Q02 / Lambda2));
        const double xP = (Q2 + mP2) / (Q2 + mP2 + W2 - M2);
        const double xR = (Q2 + mR2) / (Q2 + mR2 + W2 - M2);
        const double cP =
            cP1 + (cP1 - cP2) * (1.0 / (1.0 + pow(t, cP3)) - 1.0);
        const double aP =
            aP1 + (aP1 - aP2) * (1.0 / (1.0 + pow(t, aP3)) - 1.0);
        const double bP = bP1 + bP2 * pow(t, bP3);
        const double cR = cR1 + cR2 * pow(t, cR3);
        const double aR = aR1 + aR2 * pow(t, aR3);
        const double bR = bR1 + bR2 * pow(t, bR3);

        const double F2P = cP * exp(aP * log(xP) + bP * log(1 - x));
        const double F2R = cR * exp(aR * log(xR) + bR * log(1 - x));

        return Q2 / (Q2 + m02) * (F2P + F2R);
}

/* Shadowing factor following Dutta et al.
 *
 * @param A       The atomic charge number.
 * @param A       The atomic weight.
 * @param x       The Bjorken x parameter.
 * @return The corresponding value of the shadowing factor.
 *
 * The shadowing factor for a nucleus of atomic weight A is computed according
 * to Dutta et al.
 *
 * References:
 *      Dutta et al., Phys.Rev. D63 (2001) 094020 [arXiv:hep-ph/0012350].
 */
static inline double dcs_photonuclear_shadowing_DRSS(
    double Z, double A, double x)
{
        if (Z == 1.) return 1.;

        if (x < 0.0014)
                return exp(-0.1 * log(A));
        else if (x < 0.04)
                return exp((0.069 * log10(x) + 0.097) * log(A));
        else
                return 1.;
}

/* The F2 nuclear structure function following Dutta et al.
 *
 * @param Z            The atomic charge number.
 * @param A            The atomic weight.
 * @param F2p          The proton structure function, F2.
 * @param shadowing    The shadowing factor.
 * @param x            The Bjorken x parameter.
 * @return The corresponding value of the nuclear structure function, F2a.
 *
 * The F2a structure function for a nucleus of charge number Z and atomic
 * weight A is computed according to Dutta et al.
 *
 * References:
 *      Dutta et al., Phys.Rev. D63 (2001) 094020 [arXiv:hep-ph/0012350].
 */
static inline double dcs_photonuclear_f2a_DRSS(
    double Z, double A, double F2p, double shadowing, double x)
{
        return F2p * shadowing * (Z + (A - Z) *
            (1.0 + x * (-1.85 + x * (2.45 + x * (-2.35 + x)))));
}

/* Wood-Saxon potential.
 *
 * @param Z       The atomic charge number.
 * @param A       The atomic weight.
 * @return The corresponding value of the Wood-Saxon potential.
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/medium/Components.cxx
 */
static inline double dcs_wood_saxon(double Z, double A)
{
        if (Z == 1) {
                return 0.;
        } else {
                /* see Butkevich, Mikheyev JETP 95 (2002), 11 eq. 45-47 */
                double r0 = pow(A, 1. / 3.);
                r0 = 1.12 * r0 - 0.86 / r0;

                const double a = 0.54;
                const double zeta3 = 1.202056903159594;
                const double V = a * (r0 * r0 * log(2.) +
                    a * r0 * M_PI * M_PI / 6. + a * a * 1.5 * zeta3);

                return 1. - 4. * M_PI * 0.17 * V / A;
        }
}

/* Shadowing factor following Butkevich & Mikheyev.
 *
 * @param Z       The atomic charge number.
 * @param A       The atomic weight.
 * @param x       The Bjorken x parameter.
 * @param q       The energy lost.
 * @return The corresponding value of the shadowing factor.
 *
 * The F2a structure function for a nucleus of charge number Z and atomic
 * weight A is computed according to Butkevich & Mikheyev.
 *
 * References:
 *      Butkevich & Mikheyev, Soviet Journal of Experimental and Theoretical
 *          Physics 95 (2002) 11
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/crossection/parametrization/Photonuclear.cxx
 */
static inline double dcs_photonuclear_shadowing_BM(
    double Z, double A, double x, double q)
{
        if (Z == 1.) return 1;

        if (x > 0.3) {
                const double Mb = 0.437;
                const double la = 0.5;
                const double x2 = 0.278;

                const double au = 1. / (1. - x);
                const double ac = 1. / (1. - x2);
                /* eq. 48 */
                const double MPI = 0.13957018;
                const double M = ((A - Z) * NEUTRON_MASS + Z * PROTON_MASS) / A;
                const double Aosc = (1. - la * x) *
                    (au - ac - MPI / M  * (au * au - ac * ac));
                /* eq. 44 */
                return 1. - Mb * dcs_wood_saxon(Z, A) * Aosc;
        } else {
                const double M1 = 0.129;
                const double M2 = 0.456;
                const double M3 = 0.553;

                const double ws = dcs_wood_saxon(Z, A);
                const double m1 = M1 * ws;
                const double m2 = M2 * ws;
                const double m3 = M3 * ws;
                /* eq. 53 */
                const double sgn = 112.2 * (0.609 * pow(q, 0.0988) +
                    1.037 * pow(q, -0.5944));

                /* Bezrukav Bugaev shadow */
                const double tmp = 0.00282 * pow(A, 1. / 3.) * sgn;
                double G = (3. / tmp) * (0.5 + ((1. + tmp) * exp(-tmp) - 1.) /
                    (tmp * tmp));

                /* eq. 55 */
                G  = 0.75 * G + 0.25;
                const double x0 = pow(G / (1. + m2), 1. / m1);

                if (x >= x0) {
                        /* eq. 49 */
                        return pow(x, m1) * (1 + m2) * (1 - m3 * x);
                } else {
                        return G;
                }
        }
}

/* The F2 nuclear structure function following Butkevich & Mikheyev.
 *
 * @param Z            The atomic charge number.
 * @param A            The atomic weight.
 * @param shadowing    The shadowing factor.
 * @param x            The Bjorken x parameter.
 * @param Q2           The negative four momentum squared, in GeV.
 * @return The corresponding value of the nuclear structure function, F2a.
 *
 * The F2a structure function for a nucleus of charge number Z and atomic
 * weight A is computed according Butkevich & Mikheyev.
 *
 * References:
 *      Butkevich & Mikheyev, Soviet Journal of Experimental and Theoretical
 *          Physics 95 (2002) 11
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/crossection/parametrization/PhotoQ2Integration.cxx
 */
static inline double dcs_photonuclear_f2a_BM(
    double Z, double A, double shadowing, double x, double Q2)
{
        Q2 *= 1E+06; /*  GeV^2 to MeV^2*/

        const double a = 0.2513E+06;
        const double b = 0.6186E+06;
        const double c = 3.0292E+06;
        const double d = 1.4817E+06;
        const double intercept_pomeron = 0.0988;
        const double intercept_reggeon = 0.4056;
        const double tau = 1.8152;
        const double A_s = 0.12;
        const double B_up = 1.2437;
        const double B_down = 0.1853;

        /* Contribution of Seaquarks and Gluons (called Singlet Term)
         * eq. 26
         * n(Q^2) = 1.5 (1 + \frac{Q^2}{Q^2 + c})
         */
        const double n = 1.5 * (1. + Q2 / (Q2 + c));
        /* eq. 25
         * \Delta(Q^2) = \Delta_0 (1 + \frac{2 Q^2}{Q^2 + d})
         */
        const double dl = intercept_pomeron * (1. + 2. * Q2 / (Q2 + d));
        /* eq. 24
         * F_{2, Proton} = A_s x^{-\Delta} (1 - x)^{n + 4}
         *                 (\frac{Q^2}{Q^2 + a})^{1 + \Delta}
         */
        double aux = A_s * pow(x, -dl) * pow(Q2 / (Q2 + a), 1. + dl);
        const double F_proton_singlet = aux * pow(1. - x, n + 4.);
        /* eq. 37
         * F_{2, Neutron} = A_s x^{-\Delta} (1 - x)^{n + \tau}
         *                  (\frac{Q^2}{Q^2 + a})^{1 + \Delta}
         */
        const double F_neutron_singlet = aux * pow(1. - x, n + tau);
        /* Contribution of Valence quarks (called non-Singlet Term)
         * splitted into Up quark and down quark contributions
         * eq. 29
         * xU_v = B_{up} x^{1 - \alpha_{Reggeon}} (1 - x)^{n}
         *        (\frac{Q^2}{Q^2 + b})^{\alpha_{Reggeon}}
         */
        aux = pow(x, 1. - intercept_reggeon) * pow(1. - x, n) *
            pow(Q2 / (Q2 + b), intercept_reggeon);
        const double Up_valence = B_up * aux;
        /* eq. 30
         * xD_v = B_{down} x^{1 - \alpha_{Reggeon}} (1 - x)^{n + 1}
         *        (\frac{Q^2}{Q^2 + b})^{\alpha_{Reggeon}}
         */
        const double Down_valence = B_down * aux * (1. - x);
        /* eq. 28
         * F_{2, Proton, non-Singlet} = xU_v + xD_v
         */
        const double F_proton_non_singlet = Up_valence + Down_valence;
        /* eq. 36
         * F_{2, Neutron, non-Singlet} = \frac{1}{4} xU_v + 4 xD_v
         */
        const double F_neutron_non_singlet = Up_valence / 4. +
            Down_valence * 4.;
        /* eq. 23
         * F_{2, i} = F_{2, i, Singlet} + F_{2, i, non-Singlet}
         */
        const double structure_function_proton =
            F_proton_singlet + F_proton_non_singlet;
        const double structure_function_neutron =
            F_neutron_singlet + F_neutron_non_singlet;

        /* F_{2, nucleus} = G (Z F_{2, Proton} + (A-Z) F_{2, Neutron}) */
        return shadowing * (Z * structure_function_proton +
            (A - Z) * structure_function_neutron);
}

/** The doubly differential cross sections d^2S/(dq*dQ2) for photonuclear
 * interactions following Dutta et al.
 *
 * @param Z       The target charge number.
 * @param A       The target atomic weight.
 * @param ml      The projectile mass.
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @param Q2      The negative four momentum squared.
 * @return The doubly differential cross section in m^2/kg/GeV^3.
 *
 * References:
 *      Dutta et al., Phys.Rev. D63 (2001) 094020 [arXiv:hep-ph/0012350].
 */
static double dcs_photonuclear_d2_DRSS(
    double Z, double A, double ml, double K, double q, double Q2)
{
        const double cf = 4 * M_PI * ALPHA_EM * ALPHA_EM * HBAR_C * HBAR_C;
        const double M = 0.5 * (PROTON_MASS + NEUTRON_MASS);
        const double E = K + ml;
        const double y = q / E;
        const double x = 0.5 * Q2 / (M * q);
        const double F2p = dcs_photonuclear_f2p_ALLM97(x, Q2);
        const double shadowing = dcs_photonuclear_shadowing_DRSS(Z, A, x);
        const double F2A = dcs_photonuclear_f2a_DRSS(Z, A, F2p, shadowing, x);
        const double R = 0.;

        const double dds = (1 - y +
                               0.5 * (1 - 2 * ml * ml / Q2) *
                                   (y * y + Q2 / (E * E)) / (1 + R)) /
                (Q2 * Q2) -
            0.25 / (E * E * Q2);

        return cf * F2A * dds / q;
}

/** The doubly differential cross sections d^2S/(dq*dQ2) for photonuclear
 * interactions following Butkevich & Mikheyev.
 *
 * @param Z       The target charge number.
 * @param A       The target atomic weight.
 * @param ml      The projectile mass.
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @param Q2      The negative four momentum squared.
 * @return The doubly differential cross section in m^2/kg/GeV^3.
 *
 * References:
 *      Butkevich & Mikheyev, Soviet Journal of Experimental and Theoretical
 *          Physics 95 (2002) 11
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/crossection/parametrization/PhotoQ2Integration.cxx
 */
static double dcs_photonuclear_d2_BM(
    double Z, double A, double ml, double K, double q, double Q2)
{
        const double cf = 4 * M_PI * ALPHA_EM * ALPHA_EM * HBAR_C * HBAR_C;
        const double M = 0.5 * (PROTON_MASS + NEUTRON_MASS);
        const double E = K + ml;
        const double y = q / E;
        const double x = 0.5 * Q2 / (M * q);
        const double shadowing = dcs_photonuclear_shadowing_BM(Z, A, x, q);
        const double F2A = dcs_photonuclear_f2a_BM(Z, A, shadowing, x, Q2);
        const double R = 0.25;

        const double dds = (1 - y +
                               0.5 * (1 - 2 * ml * ml / Q2) *
                                   (y * y + Q2 / (E * E)) / (1 + R)) /
                (Q2 * Q2) -
            0.25 / (E * E * Q2);

        return cf * F2A * dds / q;
}

/**
 * The photonuclear differential cross section by integration over Q2.
 *
 * @param mode    Integration mode (see below).
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param ml      The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @param ddcs    The doubly differential cross-section.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * The photonuclear differential cross-section is computed by integration over
 * Q2 of the doubly differential cross-section using a Gaussian quadrature.
 *
 * If mode is `0` then the DCS is computed. Else the partial first transport
 * cross-section is computed.
 */
inline static double dcs_photonuclear_integrated(int mode, double Z, double A,
    double ml, double K, double q, ddcs_t * ddcs)
{
        /* Check inputs */
        if ((Z <= 0) || (A <= 0) || (ml <= 0) || (K <= 0))
                return 0.;

        const double M = 0.5 * (NEUTRON_MASS + PROTON_MASS);
        const double mpi = PION_MASS;
        if ((q <= mpi + 0.5 * mpi * mpi / M) ||
            (q >= K + ml - 0.5 * (M + ml * ml / M))) return 0.;

        const double E = K + ml;
        const double ml2 = ml * ml;
        const double Q2min = ml2 * (q * q - 0.5 * ml2) / (E * (E - q));
        const double Q2max = 2.0 * M * (q - mpi) - mpi * mpi;
        if ((Q2max < Q2min) | (Q2min < 0)) return 0.;

        /*
         * Integrate the doubly differential cross-section over Q2 using
         * a Gaussian quadrature. Note that 9 points are enough to get a
         * better than 0.1 % accuracy.
         */
        double a_mu = 1., b_mu = 0;
        if (mode) {
                const double p = sqrt(K * (K + 2 * ml));
                const double E1 = E - q;
                const double eps = ml / E1;
                const double p1 = E1 * sqrt((1. + eps) * (1. - eps));
                const double p2 = p * p1;
                double tmp = p2 + ml * ml - E * E1;
                if (fabs(tmp) <= 3 * DBL_EPSILON * p2) tmp = 0.;
                a_mu = 0.5 * tmp / p2;
                b_mu = 0.25 / p2;
        }

#define N_GQ 9
        double xGQ[N_GQ], wGQ[N_GQ];
        math_gauss_quad_initialise(N_GQ, Q2min, Q2max, 1, xGQ, wGQ);

        double ds = 0.;
        int i;
        for (i = 0; i < N_GQ; i++) {
                const double Q2 = xGQ[i];
                double tmp = ddcs(Z, A, ml, K, q, Q2);
                if (tmp <= 0.) continue;
                if (mode) {
                        double mu = a_mu + b_mu * Q2;
                        if (mu < 0.) mu = 0.;
                        else if (mu > 1.) mu = 1.;
                        tmp *= mu;
                }
                ds += tmp * wGQ[i];
        }

        return (ds < 0.) ? 0. : ds;

#undef N_GQ
}

/**
 * The photonuclear differential cross section following Dutta et al.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param m       The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * The photonuclear differential cross-section is computed following DRSS,
 * with ALLM97 parameterisation of the structure function F2.
 *
 * References:
 *      Dutta et al., Phys.Rev. D63 (2001) 094020 [arXiv:hep-ph/0012350].
 */
static double dcs_photonuclear_DRSS(double Z, double A, double m,
    double K, double q)
{
        return dcs_photonuclear_integrated(
            0, Z, A, m, K, q, &dcs_photonuclear_d2_DRSS);
}

/**
 * The photonuclear differential cross section following Butkevich & Mikheyev.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param m       The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * References:
 *      Butkevich & Mikheyev, Soviet Journal of Experimental and Theoretical
 *          Physics 95 (2002) 11
 */
static double dcs_photonuclear_BM(
    double Z, double A, double m, double K, double q)
{
        return dcs_photonuclear_integrated(
            0, Z, A, m, K, q, &dcs_photonuclear_d2_BM);
}

/**
 * Integrate the photonuclear transport.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param ml      The projectile rest mass, in GeV
 * @param kinetic The projectile initial kinetic energy.
 * @param qcut    The cut on the kinetic energy lost to the photon.
 * @param ddcs    The doubly differential cross-section.
 * @param dcs0    The physics true DCS.
 * @param ddcs    The DCS corresponding to the transport DDCS.
 * @return The corresponding value of the transport DCS, in m^2 / kg.
 */
static double dcs_photonuclear_transport_integrate(double Z, double A,
    double ml, double kinetic, double qcut, ddcs_t * ddcs, pumas_dcs_t * dcs0,
    pumas_dcs_t * dcs1)
{
        /* Set the integration boundaries */
        const double M = 0.5 * (NEUTRON_MASS + PROTON_MASS);
        const double mpi = PION_MASS;
        const double qmin = mpi + 0.5 * mpi * mpi / M;
        double qmax = kinetic + ml - 0.5 * (M + ml * ml / M);
        if (qcut < qmax) qmax = qcut;
        if (qmin >= qmax) return 0.;

        /* We integrate over the recoil energy using a logarithmic sampling. */
        const int nint = 180;
        double dcsint = 0.;
        double x0 = log(qmin), x1 = log(qmax);
        math_gauss_quad(nint, &x0, &x1); /* Initialisation. */

        double xi, wi;
        while (math_gauss_quad(0, &xi, &wi) == 0) { /* Iterations. */
                const double qi = exp(xi);
                double r = 1.;
                if (dcs0 != dcs1) {
                        const double d = dcs1(Z, A, ml, kinetic, qi);
                        if (d > 0) {
                                r = dcs0(Z, A, ml, kinetic, qi) / d;
                        } else {
                                r = 0.;
                        }
                }

                if (r > 0) {
                        const double y = dcs_photonuclear_integrated(
                            1, Z, A, ml, kinetic, qi, ddcs);
                        dcsint += y * r * qi * wi;
                }
        }

        return 2 * AVOGADRO_NUMBER / (A * 1E-03) * dcsint;
}

ddcs_t * dcs_photonuclear_ddcs(const struct pumas_physics * physics,
    pumas_dcs_t ** dcs)
{
        if (physics->dcs_photonuclear == &dcs_photonuclear_BM) {
                if (dcs != NULL) *dcs = physics->dcs_photonuclear;
                return &dcs_photonuclear_d2_BM;
        } else {
                if (dcs != NULL) *dcs = &dcs_photonuclear_DRSS;
                return &dcs_photonuclear_d2_DRSS;
        }
}

/**
 * Wrapper for photonuclear transport integral.
 */
double dcs_photonuclear_transport(const struct pumas_physics * physics,
    const struct atomic_element * element, double K, double cutoff)
{
        pumas_dcs_t * dcs;
        ddcs_t * ddcs = dcs_photonuclear_ddcs(physics, &dcs);
        return dcs_photonuclear_transport_integrate(element->Z, element->A,
            physics->mass, K, K * cutoff, ddcs, physics->dcs_photonuclear, dcs);
}

/**
 * The photon-nucleon cross-section using Kokoulin parametrization.
 *
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding cross-section, in micro-barns.
 *
 * References:
 *      R.P. Kokoulin, Nuclear Physics B Proceedings Supplements 70 (1999) 475.
 */
inline static double dcs_photonuclear_phn_Kokoulin(double q)
{
        if (q <= 17.) {
                return 96.1 + 82. / sqrt(q);
        } else if (q <= 200.) {
                const double aux = log(0.0213 * q);
                return 114.3 + 1.647 * aux * aux;
        } else {
                return 49.2 + 11.1 * log(q) + 151.8 / sqrt(q);
        }
}

/**
 * The photonuclear differential cross section following Bezrukov, Bugaev.
 *
 * @param Z       The charge number of the target atom.
 * @param A       The mass number of the target atom.
 * @param m       The projectile rest mass, in GeV
 * @param K       The projectile initial kinetic energy.
 * @param q       The kinetic energy lost to the photon.
 * @return The corresponding value of the atomic DCS, in m^2 / GeV.
 *
 * References:
 *      Bezrukov, Bugaev, Sov. J. Nucl. Phys. 33 (1981), 635.
 *      Kokoulin, Nucl. Phys. B Proc. Sup. 70 (1999) 475.
 *      Bugaev & Shlepin, Phys.Rev. D67 (2003) 034027.
 *
 * PROPOSAL implementation converted to C
 * Ref: https://github.com/tudo-astroparticlephysics/PROPOSAL/blob/master/private/PROPOSAL/crossection/parametrization/PhotoRealPhotonAssumption.cxx
 */
static double dcs_photonuclear_BBKS(
    double Z, double A, double m, double K, double q)
{
        /* Check inputs */
        if ((Z <= 0) || (A <= 0) || (m <= 0) || (K <= 0))
                return 0.;

        const double M = 0.5 * (NEUTRON_MASS + PROTON_MASS);
        const double mpi = PION_MASS;
        if ((q <= mpi + 0.5 * mpi * mpi / M) ||
            (q >= K + m - 0.5 * (M + m * m / M))) return 0.;

        const double m1 = 0.54;
        const double m2 = 1.80;

        const double sgn = dcs_photonuclear_phn_Kokoulin(q);
        const double v = q / (K + m);

        /* Calculate the shadowing factor */
        double G;
        if (Z == 1) {
                G = 1;
        } else {
                /* eq. 18 */
                const double tmp = 0.00282 * pow(A, 1. / 3) * sgn;
                /* eq. 3 */
                G = (3. / tmp) * (0.5 + ((1. + tmp) * exp(-tmp) - 1.) /
                    (tmp * tmp));
        }

        /* Enhanced formula by Bugaev Shelpin
         * Phys. Rev. D 67 (2003), 034027
         * eq. 4.6
         */
        G *= 3.;
        double aux = v * m;
        const double t = aux * aux / (1. - v);
        double aum = m;
        aum *= aum;
        aux = 2. * aum / t;
        const double kappa = 1. - 2. / v + 2. / (v * v);
        aux = G * ((kappa + 4. * aum / m1) * log(1. + m1 / t) -
            (kappa * m1) / (m1 + t) - aux) + ((kappa + 2. * aum / m2) *
            log(1. + m2 / t) - aux) + aux * (G * (m1 - 4. * t) / (m1 + t) +
            (m2 / t) * log(1. + t / m2));

        aux *= ALPHA_EM / (8. * M_PI) * A * v * sgn;

        /* Hard component by Bugaev, Montaruli, Shelpin, Sokalski
         * Astrop. Phys. 21 (2004), 491
         */
        const double (*table)[8];
        if (fabs(m - MUON_MASS) < fabs(m - TAU_MASS)) {
                /* Muon parameters */
                static const double tmp[7][8] = {
                    { 7.174409E-04, -0.2436045, -0.2942209, -0.1658391,
                      -0.05227727, -9.328318E-03, -8.751909E-04,
                      -3.343145E-05 },
                    { 1.7132E-03, -0.5756682, -0.68615, -0.3825223, -0.1196482,
                      -0.02124577, -1.987841E-03, -7.584046E-05 },
                    { 4.082304E-03, -1.553973, -2.004218, -1.207777,
                      -0.4033373, -0.07555636, -7.399682E-03, -2.943396E-04 },
                    { 8.628455E-03, -3.251305, -3.999623, -2.33175, -0.7614046,
                      -0.1402496, -0.01354059, -5.3155E-04 },
                    { 0.01244159, -5.976818, -6.855045, -3.88775, -1.270677,
                      -0.2370768, -0.02325118, -9.265136E-04 },
                    { 0.02204591, -9.495636, -10.05705, -5.636636, -1.883845,
                      -0.3614146, -0.03629659, -1.473118E-03 },
                    { 0.03228755, -13.92918, -14.37232, -8.418409, -2.948277,
                      -0.5819409, -0.059275, -2.419946E-03 }
                };
                table = tmp;
        } else {
                /* Tau parameters */
                static const double tmp[7][8] = {
                    { -1.269205E-04, -0.01563032, 0.04693954, 0.05338546,
                      0.02240132, 4.658909E-03, 4.822364E-04, 1.9837E-05 },
                    { -2.843877E-04, -0.03589573, 0.1162945, 0.130975, 0.05496,
                      0.01146659, 1.193018E-03, 4.940182E-05 },
                    { -5.761546E-04, -0.07768545, 0.3064255, 0.3410341,
                      0.144945, 0.03090286, 3.302773E-03, 1.409573E-04 },
                    { -1.195445E-03, -0.157375, 0.7041273, 0.7529364, 0.3119032,
                      0.06514455, 6.843364E-03, 2.877909E-04 },
                    { -1.317386E-03, -0.2720009, 1.440518, 1.425927, 0.5576727,
                      0.1109868, 0.011191, 4.544877E-04 },
                    { -9.689228E-15, -0.4186136, 2.533355, 2.284968, 0.8360727,
                      0.1589677, 0.015614, 6.280818E-04 },
                    { -6.4595E-15, -0.8045046, 3.217832, 2.5487, 0.8085682,
                      0.1344223, 0.01173827, 4.281932E-04 }
                };
                table = tmp;
        }

        const double E = K + m;
        if ((E > 1E+02) && (v > 1E-07)) {
                const double lE = log10(E) - 3.;
                const double lv = log10(v);
                int iE = (int)lE;

                double f[2] = {0., 0.};
                int i;
                for (i = 0; i < 2; i++, iE++) {
                        int j;
                        if (iE > 6) {
                                j = 6;
                        } else if (iE >= 0) {
                                j = iE;
                        } else {
                                j = iE;
                        }

                        const double x = (lv > -6.) ? lv : -6.;
                        const double * const t = &table[j][0];
                        f[i] = t[0] + x * (t[1] + x * (t[2] + x * (t[3] +
                            x * (t[4] + x * (t[5] + x * (t[6] + x * t[7]))))));
                        if (lv < -6.) {
                                f[i] *= 7 + lv;
                        }
                        if (lE < 0) {
                                f[i] *= 1. + lE;
                        }
                }

                const double h = lE - (int)lE;
                double fi;
                if ((f[0] > 0.) && (f[1] > 0.)) {
                        /* log-log interpolation */
                        fi = exp(log(f[0]) * (1. - h) + log(f[1]) * h);
                } else {
                        /* log-linear interpolation */
                        fi = f[0] * (1. - h) + f[1] * h;
                }
                aux += A * fi / v;
        }

        return aux * 1E-34 / (K + m);
}

/** Data structure for caracterising a DCS model */
struct dcs_entry {
        enum pumas_process process;
        const char * model;
        pumas_dcs_t * dcs;
};

/** Stack (library) of available DCS models
 *
 * Note that the first entry for a given process is taken as the default
 * model for the corresponding process.
 */
#define DCS_STACK_SIZE 64
static struct dcs_entry dcs_stack[DCS_STACK_SIZE] = {
    {PUMAS_PROCESS_BREMSSTRAHLUNG,  "KKP",  &dcs_bremsstrahlung_KKP},
    {PUMAS_PROCESS_BREMSSTRAHLUNG,  "ABB",  &dcs_bremsstrahlung_ABB},
    {PUMAS_PROCESS_BREMSSTRAHLUNG,  "SSR",  &dcs_bremsstrahlung_SSR},
    {PUMAS_PROCESS_PAIR_PRODUCTION, "KKP",  &dcs_pair_production_KKP},
    {PUMAS_PROCESS_PAIR_PRODUCTION, "SSR",  &dcs_pair_production_SSR},
    {PUMAS_PROCESS_PHOTONUCLEAR,    "DRSS", &dcs_photonuclear_DRSS},
    {PUMAS_PROCESS_PHOTONUCLEAR,    "BM",   &dcs_photonuclear_BM},
    {PUMAS_PROCESS_PHOTONUCLEAR,    "BBKS", &dcs_photonuclear_BBKS}
};

/** Mapping between enum and names for processes */
static const char * process_name[3] = {
        "bremsstrahlung", "pair production", "photonuclear"};

/** Routine for checking if a model's name exists */
static enum pumas_return dcs_check_model(enum pumas_process process,
     const char * model, struct error_context * error_)
{
        int i;
        struct dcs_entry * entry;
        for (i = 0, entry = dcs_stack;
            (i < DCS_STACK_SIZE) && (entry->model != NULL); i++, entry++) {
                if ((entry->process == process) &&
                    (strcmp(entry->model, model) == 0)) {
                        return PUMAS_RETURN_SUCCESS;
                }
        }

        return ERROR_VREGISTER(PUMAS_RETURN_MODEL_ERROR,
            "cannot find %s model for %s process", model,
            process_name[process]);
}

/** Routine for checking a process index */
static enum pumas_return dcs_check_process(
    enum pumas_process process, struct error_context * error_)
{
        if ((process < 0) || (process > PUMAS_PROCESS_PHOTONUCLEAR)) {
                return ERROR_VREGISTER(PUMAS_RETURN_INDEX_ERROR,
                    "bad process (expected an index in [0, %d], got %u)",
                    PUMAS_PROCESS_PHOTONUCLEAR, process);
        } else {
                return PUMAS_RETURN_SUCCESS;
        }
}

/* API function for registering a DCS model */
enum pumas_return pumas_dcs_register(
    enum pumas_process process, const char * model, pumas_dcs_t * dcs)
{
        ERROR_INITIALISE(pumas_dcs_register);

        /* Check the process index */
        if (dcs_check_process(process, error_) != PUMAS_RETURN_SUCCESS) {
                return ERROR_RAISE();
        }

        /* Check that a DCS function was actually provided */
        if (dcs == NULL) {
                return ERROR_MESSAGE(PUMAS_RETURN_VALUE_ERROR,
                    "bad dcs (expected a function, got nil)");
        }

        /* Check if the model is already registered */
        if (model == NULL) {
                return ERROR_MESSAGE(PUMAS_RETURN_VALUE_ERROR,
                    "bad model (expected a string, got nil)");
        }

        int i;
        struct dcs_entry * entry;
        for (i = 0, entry = dcs_stack;
            (i < DCS_STACK_SIZE) && (entry->model != NULL); i++, entry++) {
                if ((entry->process == process) &&
                    (strcmp(entry->model, model) == 0)) {
                        return ERROR_FORMAT(PUMAS_RETURN_MODEL_ERROR,
                            "model %s already registered for %s process",
                            model, process_name[process]);
                }
        }
        if (i == DCS_STACK_SIZE) {
                return ERROR_MESSAGE(PUMAS_RETURN_MEMORY_ERROR,
                    "max stack size reached");
        }

        /* Append the new DCS */
        entry->process = process;
        entry->model = model;
        entry->dcs = dcs;

        return PUMAS_RETURN_SUCCESS;
}

/* API function for getting a DCS model */
enum pumas_return pumas_dcs_get(
    enum pumas_process process, const char * model, pumas_dcs_t ** dcs)
{
        ERROR_INITIALISE(pumas_dcs_get);

        /* Check the process index */
        if (dcs_check_process(process, error_) != PUMAS_RETURN_SUCCESS) {
                return ERROR_RAISE();
        }

        /* Set the default model if none provided */
        if (model == NULL) {
                if (process == PUMAS_PROCESS_BREMSSTRAHLUNG)
                        model = DEFAULT_BREMSSTRAHLUNG;
                else if (process == PUMAS_PROCESS_PAIR_PRODUCTION)
                        model = DEFAULT_PAIR_PRODUCTION;
                else
                        model = DEFAULT_PHOTONUCLEAR;
        }

        /* Look for the model */
        int i;
        struct dcs_entry * entry;
        for (i = 0, entry = dcs_stack;
            (i < DCS_STACK_SIZE) && (entry->model != NULL); i++, entry++) {
                if ((entry->process == process) && 
                    (strcmp(entry->model, model) == 0)) {
                        *dcs = entry->dcs;
                        return PUMAS_RETURN_SUCCESS;
                }
        }
        *dcs = NULL;

        return ERROR_FORMAT(PUMAS_RETURN_MODEL_ERROR,
            "model %s not found for %s process",
            model, process_name[process]);
}

/* API function for getting the kinematic range of a DCS model */
enum pumas_return pumas_dcs_range(enum pumas_process process, double Z,
    double mass, double energy, double * min, double * max)
{
        ERROR_INITIALISE(pumas_dcs_range);

        /* Check the process index */
        if (dcs_check_process(process, error_) != PUMAS_RETURN_SUCCESS) {
                return ERROR_RAISE();
        }

        if (process == PUMAS_PROCESS_PHOTONUCLEAR) {
                dcs_photonuclear_range(mass, energy, min, max);
        } else {
                dcs_pair_production_range(Z, mass, energy, min, max);
                if ((process == PUMAS_PROCESS_BREMSSTRAHLUNG) &&
                    (min != NULL)) {
                        *min = 0.;
                }
        }

        return PUMAS_RETURN_SUCCESS;
}

/* API function for getting the name of the default DCS model */
const char * pumas_dcs_default(enum pumas_process process)
{
        if (process == PUMAS_PROCESS_BREMSSTRAHLUNG)
                return DEFAULT_BREMSSTRAHLUNG;
        else if (process == PUMAS_PROCESS_PAIR_PRODUCTION)
                return DEFAULT_PAIR_PRODUCTION;
        else if (process == PUMAS_PROCESS_PHOTONUCLEAR)
                return DEFAULT_PHOTONUCLEAR;
        else
                return NULL;
}

/** Public library function: the elastic differential cross section. */
double pumas_elastic_dcs(
    double Z, double A, double m, double K, double theta)
{
        if ((Z < 1.) || (A < 1.) || (m <= 0.) || (K <= 0.)) return 0.;

        int n_parameters;
        double kinetic0, amplitude[3], screening[5], fCM[2];
        coulomb_frame_parameters(Z, A, m, K, &kinetic0, fCM);
        coulomb_screening_parameters(
            Z, A, m, K, kinetic0, &n_parameters, amplitude, screening);
        const double fspin = coulomb_spin_factor(m, K);

        const double c = cos(theta);
        const double s = sin(theta);
        const double gs = fCM[0] * s;
        const double sd = sqrt(c * c + gs * gs * (1. - fCM[1] * fCM[1]));
        const double cgs = c * c + gs * gs;
        const double mu0 = 0.5 * (gs * gs * (1. + fCM[1]) + c * (c - sd)) / cgs;

        const double num = fCM[0] * (c * fCM[1] + sd);
        const double jac = num * num / (cgs * cgs * sd);

        const int n = n_parameters - 1;
        double dcs = 0.;
        int i;
        for (i = 0; i < n; i++) {
                dcs += amplitude[i] / (screening[i] + mu0);
        }
        dcs *= coulomb_nuclear_form_factor(mu0, screening[n]);
        dcs *= dcs;

        const double p0 = sqrt(kinetic0 * (kinetic0 + 2. * m));
        const double p = sqrt(K * (K + 2. * m));
        const double factor = 0.5 * ALPHA_EM * Z * (K + m) * HBAR_C / (p0 * p);

        return factor * factor * dcs * jac * (1. - fspin * mu0);
}

/* Public library function: elastic scattering path length. */
double pumas_elastic_path(
    int order, double Z, double A, double mass, double kinetic)
{
        if ((order < 0) || (order > 1) || (kinetic <= 0.)) return 0.;

        int n_parameters;
        double kinetic0, amplitude[3], screening[4], coefficient[2], fCM[2];
        coulomb_frame_parameters(Z, A, mass, kinetic, &kinetic0, fCM);
        coulomb_screening_parameters(Z, A, mass, kinetic, kinetic0,
            &n_parameters, amplitude, screening);
        const double fspin = coulomb_spin_factor(mass, kinetic);
        long double a[3], b[3], c[4];
        coulomb_pole_reduction(
            n_parameters, amplitude, screening, a, b, c);
        coulomb_transport_coefficients(
            1., fspin, n_parameters, screening, a, b, c, coefficient);
        if (order == 1) {
                const double d = 1. / (fCM[0] * (1. + fCM[1]));
                coefficient[order] *= d * d;
        }

        return coulomb_normalisation(Z, A, mass, kinetic, kinetic0) /
            coefficient[order];
}
