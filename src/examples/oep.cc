/*
  This file is part of MADNESS.

  Copyright (C) 2007,2010 Oak Ridge National Laboratory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680
*/

//#define WORLD_INSTANTIATE_STATIC_TEMPLATES

/*!
  \file examples/oep.cc
  \brief optimized effective potentials for DFT
*/

#include <chem/nemo.h>
#include <chem/cheminfo.h>
#include <chem/SCFOperators.h>
#include <chem/projector.h>

using namespace madness;

struct dens_inv{

	double threshold;

	dens_inv(const double thresh = 1.0e-8) { // same default value as for dens_thresh
		threshold = thresh;
	}

    /// @param[out] U   result
    /// @param[in]  t   numerator
    /// @param[in]  inv density to be inverted >0.0
    void operator()(const Key<3>& key, Tensor<double>& U, const Tensor<double>& t,
            const Tensor<double>& inv) const {
        ITERATOR(
            U, double d = t(IND);
            double p = std::max(inv(IND), threshold);
            U(IND) = d/p;
        );
   }
    template <typename Archive>
    void serialize(Archive& ar) {}

};


/// TODO: change dens_inv such that Slater potential is munged unsing the following form:

/*
/// Class to compute terms of the potential
struct xc_potential {
    const XCfunctional* xc;
    const int ispin;

    xc_potential(const XCfunctional& xc, int ispin) : xc(&xc), ispin(ispin)
    {}



    std::vector<madness::Tensor<double> > operator()(const madness::Key<3> & key,
            const std::vector< madness::Tensor<double> >& t) const {
        MADNESS_ASSERT(xc);
        std::vector<madness::Tensor<double> > r = xc->vxc(t, ispin);
        return r;
    }
};
*/


struct binary_munge{

	double longrangevalue;
	double threshold;

	/// same default value as for munge_thresh
	binary_munge(const double thresh = 1.0e-8, const double lrv = 0.0) {
		longrangevalue = lrv;
		threshold = thresh;
	}

    /// @param[out] U   result
    /// @param[in]  f   function to be munged
    /// @param[in]  r   refdensity
    void operator()(const Key<3>& key, Tensor<double>& U, const Tensor<double>& f,
            const Tensor<double>& refdens) const {
        ITERATOR(
            U, double r = refdens(IND);
            double ff = f(IND);
            U(IND) = (r > threshold) ? ff : longrangevalue;
        );
    }

    template <typename Archive>
    void serialize(Archive& ar) {}

};


struct binary_munge_linear{

	double longrangevalue, thresh_high, thresh_low;

	/// same default values as for dens_thresh_hi and dens_thresh_lo
	binary_munge_linear(const double hi = 1.0e-4, const double lo = 1.0e-7, const double lrv = 0.0) {
		longrangevalue = lrv;
		thresh_high = hi;
		thresh_low = lo;
	}

    /// @param[out] U   result
    /// @param[in]  f   function to be munged
    /// @param[in]  r   refdensity
    void operator()(const Key<3>& key, Tensor<double>& U, const Tensor<double>& f,
            const Tensor<double>& refdens) const {

    	// interpolate with value = (r - lo)/(hi - lo), then U = f*value + longrangevalue*(1 - value)
    	const Tensor<double> value = (refdens - thresh_low)/(thresh_high - thresh_low);
    	Tensor<double> ff = copy(f);
    	ff.emul(value);
    	Tensor<double> longrange = longrangevalue*(1.0 - value);

        ITERATOR(
            U, double r = refdens(IND);
            if (r > thresh_high) {
            	U(IND) = f(IND);
            } else if (r < thresh_low) {
            	U(IND) = longrangevalue;
            } else {
            	U(IND) = ff(IND) + longrange(IND);
            }
        );
    }

    template <typename Archive>
    void serialize(Archive& ar) {}

};


/// simple structure to take the pointwise logarithm of a function, shifted by +14
struct logme{
    typedef double resultT;
    struct logme1 {
        double operator()(const double& val) {return log(std::max(1.e-14,val))+14.0;}
    };
    Tensor<double> operator()(const Key<3>& key, const Tensor<double>& val) const {
        Tensor<double> result=copy(val);
        logme1 op;
        return result.unaryop(op);
    }

    template <typename Archive>
    void serialize(Archive& ar) {}
};


class OEP : public Nemo {

	typedef std::shared_ptr<real_convolution_3d> poperatorT;

private:

	double dens_thresh_hi = 1.0e-4;   // default 1.0e-4
	double dens_thresh_lo = 1.0e-7;   // default 1.0e-7
	double munge_thresh = 1.0e-8;     // default 1.0e-8
	unsigned int damp_num = 0;        // default 0 (no damping)
	std::vector<double> damp_coeff;
	std::string model;
	std::vector<bool> oep_model = {false, false, false};
	bool save_nemo_squares = false;                   // if true save density contributions of orbitals
	unsigned int save_iter_density = 0;               // if > 0 save KS density every ... iterations
	unsigned int save_iter_IKS = 0;                   // if > 0 save IKS every ... iterations
	unsigned int save_iter_kinKS = 0;                 // if > 0 save kinKS every ... iterations
	unsigned int save_iter_ocep_correction = 0;       // if > 0 save OCEP correction every ... iterations
	unsigned int save_iter_dcep_correction = 0;       // if > 0 save DCEP correction every ... iterations
	unsigned int save_iter_total_correction = 0;      // if > 0 save total correction (OCEP + DCEP) every ... iterations
	unsigned int save_iter_effective_potential = 0;   // if > 0 save effective potential every ... iterations

	void set_model_oaep() {oep_model[0] = true;}
	void unset_model_oaep() {oep_model[0] = false;}
	bool is_oaep() const {return oep_model[0];}
	void set_model_ocep() {oep_model[1] = true;}
	void unset_model_ocep() {oep_model[1] = false;}
	bool is_ocep() const {return oep_model[1];}
	void set_model_dcep() {oep_model[2] = true;}
	void unset_model_cdep() {oep_model[2] = false;}
	bool is_dcep() const {return oep_model[2];}

	/// returns true if all members of a vector of booleans are true, otherwise false
	bool IsAlltrue(std::vector<bool> vec) {
		for (int i = 0; i < vec.size(); i++) {
			if (!vec[i]) return false;
		}
		return true;
	}

public:

	OEP(World& world, const std::shared_ptr<SCF> calc) : Nemo(world, calc) {}

	void read_oep_param(std::istream& in) {
        position_stream(in, "oep");
        std::string str;

        while (in >> str) {
            if (str == "end") {
                break;
            }
            else if (str == "model") {
            	in >> model;
            }
            else if (str == "save_nemo_squares") {
            	save_nemo_squares = true;
            }
            else if (str == "save_density") {
            	in >> save_iter_density;
            }
            else if (str == "save_IKS") {
            	in >> save_iter_IKS;
            }
            else if (str == "save_kin_KS") {
            	in >> save_iter_kinKS;
            }
            else if (str == "save_OCEP_correction") {
            	in >> save_iter_ocep_correction;
            }
            else if (str == "save_DCEP_correction") {
            	in >> save_iter_dcep_correction;
            }
            else if (str == "save_total_correction") {
            	in >> save_iter_total_correction;
            }
            else if (str == "save_effective_potential") {
            	in >> save_iter_effective_potential;
            }
            else if (str == "density_threshold_high") {
            	in >> dens_thresh_hi;
            }
            else if (str == "density_threshold_low") {
            	in >> dens_thresh_lo;
            }
            else if (str == "munge_threshold") {
            	in >> munge_thresh;
            }
            else if (str == "damping") {
            	in >> damp_num;
            	for (unsigned int i = 0; i < damp_num + 1; i++) {
            		double coeff;
            		in >> coeff;
            		damp_coeff.push_back(coeff);
            	}
            }
            else {
                print("oep: unrecognized input keyword:", str);
                MADNESS_EXCEPTION("input error",0);
            }
        }

        // set variables from input and print notes in output

    	if (model == "oaep" or model == "OAEP") {
    		set_model_oaep();
    		model = "OAEP";
    	} else if (model == "ocep" or model == "OCEP") {
    		set_model_ocep();
    		model = "OCEP";
    	} else if (model == "dcep" or model == "DCEP") {
    		set_model_dcep();
    		model = "DCEP";
    	} else {
            print("oep: no approximate OEP model selected, please choose oaep/ocep/dcep!");
            MADNESS_EXCEPTION("input error",0);
    	}

    	print("using", model, "model as approximation to OEP");
    	print("using upper density threshold =", dens_thresh_hi);
    	print("using lower density threshold =", dens_thresh_lo);
    	print("using munge threshold =", munge_thresh);
    	if (damp_num == 0) {
    		damp_coeff.push_back(1.0);
    		print("using no damping");
    	}
    	else {
    		print("using damping with", damp_num, "old potential(s) and the following coefficients:");
    		print("         new potential =", damp_coeff[0]);
    		for (unsigned int i = 1; i < damp_num + 1; i++) {
    			print("  previous potential", i, "=", damp_coeff[i]);
    		}
    	}
    	print("");

        // check some common mistakes in input file

        if (dens_thresh_hi <= dens_thresh_lo) {
            print("oep: density_threshold_high must always be larger than density_threshold_low!");
            MADNESS_EXCEPTION("input error",0);
        }

        double all_coeffs = 0.0;
        for (unsigned int i = 0; i < damp_num + 1; i++) {
        	all_coeffs += damp_coeff[i];
        }
        if (all_coeffs != 1.0) {
            print("oep: sum of damping coefficients does not equal 1.0, please check the input file!");
            MADNESS_EXCEPTION("input error",0);
        }
	}

    /// Iterative energy calculation for approximate OEP with EXACT EXCHANGE functional
	/// for other functionals, slater potential must be modified
	/// HF orbitals and eigenvalues are used as the guess here
	/// note that KS_nemo is a reference and changes oep->get_calc()->amo orbitals
	/// same for orbital energies (eigenvalues) KS_eigvals which is oep->get_calc()->aeps
	/// converged if norm, total energy difference and orbital energy differences (if not OAEP) are converged
    void solve_oep(const vecfuncT& HF_nemo, const tensorT& HF_eigvals) {

    	double energy = 0.0;
    	bool converged = false;
    	unsigned int iter_counter = 0;

		// compute Slater potential Vs and average IHF from HF orbitals and eigenvalues
    	const real_function_3d Vs = compute_slater_potential(HF_nemo, homo_ind(HF_eigvals));
    	const real_function_3d IHF = compute_average_I(HF_nemo, HF_eigvals);
    	save(IHF, "IHF");
    	const real_function_3d kinHF = compute_kinetic_term(HF_nemo, HF_eigvals);
    	save(kinHF, "kin_HF");

    	// set KS_nemo as reference to MOs
    	vecfuncT& KS_nemo = calc->amo;
    	tensorT& KS_eigvals = calc->aeps; // 1d tensor of same length as KS_nemo
		save(compute_density(HF_nemo), "density_HF");
    	save(compute_density(KS_nemo), "density_start");

    	// if desired: print HF orbital contributions to total density (nemo squares)
    	if (save_nemo_squares) {
        	vecfuncT HF_nemo_square = square(world, HF_nemo);
        	for (int i = 0; i < HF_nemo_square.size(); i++) {
        		save(HF_nemo_square[i], "HF_nemo_square_" + stringify(i));
        	}
    	}

    	// all necessary operators applied on nemos
    	vecfuncT Jnemo, Unemo, Vnemo, Knemo;
    	real_function_3d Voep = Vs;

    	// copy Vs to all old potentials for damping
       	std::vector<real_function_3d> Voep_old;
       	for (unsigned int i = 0; i < damp_num; i++) {
       		Voep_old.push_back(Vs);
       	}

    	// define the solver
    	typedef allocator<double, 3> allocT;
    	typedef XNonlinearSolver<vecfunc<double, 3>, double, allocT> solverT;
    	allocT alloc(world, KS_nemo.size());
    	solverT solver(allocT(world, KS_nemo.size()));

    	// iterate until self-consistency
    	for (int iter = 0; iter < calc->param.maxiter; ++iter) {
    		iter_counter++;
    		print("\n     ***", model, "iteration", iter_counter, "***\n");

    		if (is_ocep() or is_dcep()) {

    			// damping for better convergence of Voep
    			if (damp_num > 0) {
        			for (unsigned int i = 0; i < damp_num - 1; i++) {
        				Voep_old[i+1] = Voep_old[i];
        			}
        			Voep_old[0] = Voep;
    			}

        		// compute OCEP potential from current nemos and eigenvalues
    			real_function_3d corr_ocep = compute_ocep_correction(HF_eigvals, IHF, KS_nemo, KS_eigvals);
    			real_function_3d corr_dcep = compute_dcep_correction(kinHF, KS_nemo, KS_eigvals);

    			// and shift potential so that HOMO_HF = HOMO_KS, so potential += (HOMO_HF - HOMO_KS)
    			double shift = homo_diff(HF_eigvals, KS_eigvals);
    			print("building new Veop: orbital shift is", shift, "Eh");

    			// damping
    			Voep = damp_coeff[0]*(Vs + corr_ocep + shift);
    			if (is_dcep()) Voep += damp_coeff[0]*corr_dcep;
    			for (unsigned int i = 0; i < damp_num; i++) {
    				Voep += damp_coeff[i + 1]*Voep_old[i];
    			}

    			// save certain functions if desired
    			if (save_iter_density > 0) {
    				if (iter_counter == 2 or iter_counter % save_iter_density == 0) {
    					save(compute_density(KS_nemo), "density_iter_" + stringify(iter_counter));
    				}
    			}
    			if (save_iter_IKS > 0) {
    				if (iter_counter == 2 or iter_counter % save_iter_IKS == 0) {
    					save(compute_average_I(KS_nemo, KS_eigvals), "IKS_iter_" + stringify(iter_counter));
    				}
    			}
    			if (save_iter_kinKS > 0) {
    				if (iter_counter == 2 or iter_counter % save_iter_kinKS == 0) {
    					save(compute_kinetic_term(KS_nemo, KS_eigvals), "kin_KS_iter_" + stringify(iter_counter));
    				}
    			}
    			if (save_iter_ocep_correction > 0) {
    				if (iter_counter == 2 or iter_counter % save_iter_ocep_correction == 0) {
    					save(corr_ocep + shift, "OCEP_correction_iter_" + stringify(iter_counter));
    				}
    			}
    			if (save_iter_dcep_correction > 0) {
    				if (iter_counter == 2 or iter_counter % save_iter_dcep_correction == 0) {
    					save(corr_dcep + shift, "DCEP_correction_iter_" + stringify(iter_counter));
    				}
    			}
    			if (save_iter_total_correction > 0) {
    				if (iter_counter == 2 or iter_counter % save_iter_total_correction == 0) {
    					save(corr_ocep + corr_dcep + shift, "total_correction_iter_" + stringify(iter_counter));
    				}
    			}
    			if (save_iter_effective_potential > 0) {
    				if (iter_counter == 2 or iter_counter % save_iter_effective_potential == 0) {
    					save(Voep, "effective_potential_iter_" + stringify(iter_counter));
    				}
    			}

			}

//			for (int i = 0; i < KS_nemo.size(); i++) {
//			save(KS_nemo[i], "KS_iter_"+stringify(iter)+"_nemo_"+stringify(i));
//			}

			vecfuncT R2KS_nemo = R_square*KS_nemo;
			truncate(world, R2KS_nemo);

    		// compute parts of the Fock matrix J, Unuc and Voep
    		compute_nemo_potentials(KS_nemo, Jnemo, Unemo, Voep, Vnemo);

    		// compute Fock matrix F = J + Voep + Vnuc and kinetic energy
    		vecfuncT Fnemo = Jnemo + Vnemo + Unemo;
    		truncate(world, Fnemo);
    		tensorT F = matrix_inner(world, R2KS_nemo, Fnemo, false); // matrix_inner gives 2d tensor
    		Kinetic<double,3> T(world);
    		F += T(R2KS_nemo, KS_nemo); // 2d tensor = Fock-matrix  // R_square in bra, no R in ket

    		// report the off-diagonal Fock matrix elements because canonical orbitals are used
            tensorT F_offdiag = copy(F);
            for (int i = 0; i < F.dim(0); ++i) F_offdiag(i, i) = 0.0;
            double max_F_offidag = F_offdiag.absmax();
            if (world.rank() == 0) print("F max off-diagonal ", max_F_offidag);

    		// compute new (current) energy
            double old_energy = energy;
            print("energy contributions of iteration", iter_counter);
    		energy = compute_energy(R*KS_nemo, R*Jnemo, Voep, Knemo, true); // Knemo is not used here
    		// compute_exchange_potential(KS_nemo, Knemo);
    		// energy = compute_energy(R*KS_nemo, R*Jnemo, Voep, R*Knemo, false);
    		// there should be no difference between these two methods, because energy is only needed
    		// for checking convergence threshold; but: Evir should be much faster because K is expensive

    		// copy old orbital energies for convergence criterium at the end
    		tensorT old_eigvals = copy(KS_eigvals);

            // diagonalize the Fock matrix to get the eigenvalues and eigenvectors (canonical)
    		// FC = epsilonSC and X^dSX with transform matrix X, see Szabo/Ostlund (3.159) and (3.165)
            tensorT X; // must be formed from R*nemos but can then be used for nemos also
            tensorT overlap = matrix_inner(world, R*KS_nemo, R*KS_nemo, true);
            X = calc->get_fock_transformation(world, overlap, F, KS_eigvals, calc->aocc,
            		FunctionDefaults<3>::get_thresh());
            KS_nemo = transform(world, KS_nemo, X, trantol(), true);
            rotate_subspace(world, X, solver, 0, KS_nemo.size());

            truncate(world, KS_nemo);
            normalize(KS_nemo);

    		// calculate new orbital energies (current eigenvalues from Fock-matrix)
    		for (int i = 0; i < KS_nemo.size(); ++i) {
    			KS_eigvals(i) = std::min(-0.05, F(i, i)); // orbital energy is set to -0.05 if it was above
    		}

    		/// TODO: Question: is this necessary in our programme or even bad?
    		// if requested: subtract orbital shift from orbital energies
    		if (calc->param.orbitalshift > 0.0) {
    			if (world.rank() == 0) print("shifting orbitals by ",
    					calc->param.orbitalshift, " to lower energies");
    			KS_eigvals -= calc->param.orbitalshift;
    		}

    		// print orbital energies:
    		print("orbital energies of iteration", iter_counter);
    		print_orbens(KS_eigvals);
    		print("HF/KS HOMO energy difference of", homo_diff(HF_eigvals, KS_eigvals), "Eh is not yet included");

    		// construct the BSH operators ops
    		std::vector<poperatorT> G = calc->make_bsh_operators(world, KS_eigvals);

    		// remember Fock matrix * nemos from above; make sure it's in phase with nemo (transform)
    		Fnemo = transform(world, Fnemo, X, trantol(), true);
    		truncate(world, Fnemo);

    		// apply the BSH operators G (here ops) on the wave function
    		scale(world, Fnemo, -2.0);
    		vecfuncT GFnemo = apply(world, G, Fnemo);
    		truncate(world, GFnemo);

    		double n1 = norm2(world, KS_nemo);
    		double n2 = norm2(world, GFnemo);
    		print("\nnorm of nemo and GFnemo, ratio ", n1, n2, n1/n2);

    		// compute the residuals for KAIN
    		vecfuncT residual = KS_nemo - GFnemo;
    		const double norm = norm2(world, residual) / sqrt(KS_nemo.size());

    		// KAIN (helps to converge)
    		vecfuncT nemo_new;
    		if (norm < 5.0e-1) {
    			nemo_new = (solver.update(KS_nemo, residual)).x;
    		} else {
    			nemo_new = GFnemo;
    		}
    		truncate(world, nemo_new);
    		normalize(nemo_new);

    		// What is step restriction?
    		calc->do_step_restriction(world, KS_nemo, nemo_new, "ab spin case");
    		orthonormalize(nemo_new);
    		KS_nemo = nemo_new;

    		// evaluate convergence via norm error and energy difference
    		if ((norm < calc->param.dconv) and (fabs(energy - old_energy) < calc->param.econv)) {

    			if (is_oaep()) converged = true;  // if OAEP, the following evaluation is not necessary
    			else {
    				// build vector of convergence information of every orbital energy
        			std::vector<bool> conv(KS_eigvals.size());
        			for (long i = 0; i < KS_eigvals.size(); i++) {
        				if (fabs(KS_eigvals(i) - old_eigvals(i)) < calc->param.dconv) conv[i] = true;
        				else conv[i] = false;
        			}

        			if (IsAlltrue(conv)) converged = true; // converged if all are converged
    			}

    		}

    		if (calc->param.save) calc->save_mos(world);

    		if (world.rank() == 0) {
    			printf("\nfinished iteration %2d at time %8.1fs with energy %12.8f\n", iter_counter, wall_time(), energy);
    			print("current residual norm", norm, "\n");
    		}

    		if (converged) break;

    	}

    	if (converged) {
    		if (world.rank() == 0) {
    			print("\n     +++ Iterations converged +++\n");
    			print(model, "converged after", iter_counter, "iterations\n\n");
    		}
    	}
    	else {
    		if (world.rank() == 0) print("\n     --- Iterations failed ---\n\n");
    		energy = 0.0;
    	}

    	// calculate and print all final numbers
    	print("\n  computing final IKS and density");

    	// if desired: print final KS orbital contributions to total density (nemo squares)
    	if (save_nemo_squares) {
        	vecfuncT KS_nemo_square = square(world, KS_nemo);
        	for (int i = 0; i < KS_nemo_square.size(); i++) {
        		save(KS_nemo_square[i], "KS_nemo_final_square_" + stringify(i));
        	}
    	}

    	real_function_3d IKS = compute_average_I(KS_nemo, KS_eigvals);
    	real_function_3d rho = compute_density(KS_nemo);
    	save(rho, "density_final");
    	save(IKS, "IKS_final");
    	print("     done");

    	if (is_oaep()) {
    		print("\n  computing OCEP with converged OAEP orbitals and eigenvalues");
        	real_function_3d correction = compute_ocep_correction(HF_eigvals, IHF, KS_nemo, KS_eigvals);
        	real_function_3d ocep_oaep_pot = Vs + correction;
        	save(correction, "OCEP_correction");
        	save(ocep_oaep_pot, "OCEP_potential_with_OAEP_orbs");
    	}
    	if (is_ocep()) {
    		print("\n  computing final OCEP with converged OCEP orbitals and eigenvalues");
        	real_function_3d correction_final = compute_ocep_correction(HF_eigvals, IHF, KS_nemo, KS_eigvals);
        	Voep = Vs + correction_final;
        	save(correction_final, "OCEP_correction_final");
        	save(Voep, "OCEP_final");
    	}
    	if (is_dcep()) {
    		print("\n  computing final DCEP with converged DCEP orbitals and eigenvalues");
        	real_function_3d ocep_correction_final = compute_ocep_correction(HF_eigvals, IHF, KS_nemo, KS_eigvals);
        	real_function_3d dcep_correction_final = compute_dcep_correction(kinHF, KS_nemo, KS_eigvals);
        	Voep = Vs + ocep_correction_final + dcep_correction_final;
        	save(ocep_correction_final, "OCEP_correction_final");
        	save(dcep_correction_final, "DCEP_correction_final");
        	save(ocep_correction_final + dcep_correction_final, "total_correction_final");
        	save(Voep, "DCEP_final");
    	}
    	print("     done\n");

    	// print final orbital energies
   		print("final shifted", model, "orbital energies:");
   		print_orbens(KS_eigvals, homo_diff(HF_eigvals, KS_eigvals));
   		print("HF/KS HOMO energy difference of", homo_diff(HF_eigvals, KS_eigvals), "Eh is already included\n");

    	print("FINAL", model, "ENERGY Evir:");
    	double Evir = compute_energy(R*KS_nemo, R*Jnemo, Voep, Knemo, true); // Knemo is not used here

    	print("FINAL", model, "ENERGY Econv:");
    	compute_exchange_potential(KS_nemo, Knemo);
    	double Econv = compute_energy(R*KS_nemo, R*Jnemo, Voep, R*Knemo, false); // Voep is not used here

    	printf("      Evir = %15.8f  Eh", Evir);
    	printf("\n     Econv = %15.8f  Eh", Econv);
    	printf("\n     DEvir = %15.8f mEh\n\n", (Evir - Econv)*1000);

    }

    /// get index of HOMO from a given set of orbital energies
    long homo_ind(const tensorT orbens) const {
    	long index;
    	double en_homo = orbens.max(&index);
    	return index;
    }

    /// get difference of HF and KS HOMO energies as HOMO_KS - HOMO_HF
    double homo_diff(const tensorT ev1, const tensorT ev2) const {
    	return ev1(homo_ind(ev1)) - ev2(homo_ind(ev2));
    }

    /// print orbital energies in reverse order with optional shift
    void print_orbens(const tensorT orbens, const double shift = 0.0) const {
		for (long i = orbens.size() - 1; i >= 0; i--) {
			printf(" e%2.2lu = %12.8f Eh\n", i, orbens(i) + shift);
		}
    }

    /// compute density from orbitals with ragularization (Bischoff, 2014_1, equation (19))
    real_function_3d compute_density(const vecfuncT& nemo) const {
    	real_function_3d density = 2.0*R_square*dot(world, nemo, nemo); // 2 because closed shell
    	return density;
    }

    /// compute Slater potential (Kohut, 2014, equation (15))
    real_function_3d compute_slater_potential(const vecfuncT& nemo, const long homo_ind) const {

        Exchange K(world, this, 0); // no - in K here, so factor -1 must be included at the end
        vecfuncT Knemo = K(nemo);
        real_function_3d numerator = 2.0*R_square*dot(world, nemo, Knemo); // 2 because closed shell
        real_function_3d rho = compute_density(nemo);

        // dividing by rho: the minimum value for rho is dens_thresh
        real_function_3d Vs = -1.0*binary_op(numerator, rho, dens_inv(dens_thresh_lo));
        save(Vs, "Slaterpotential_nolra");

        // long-range asymptotic behavior for Slater potential is \int 1/|r-r'| * |phi_HOMO|^2 dr'
        // in order to compute this lra, use Coulomb potential with only HOMO density (= |phi_HOMO|^2)
        Coulomb J(world, this);
        real_function_3d lra = -1.0*J.compute_potential(R_square*square(nemo[homo_ind]));

//        // these are not yet forgotten tests
//        real_function_3d lra = (-0.5/nemo.size())*J.compute_potential(this);

//        for (int i = 0; i < nemo.size(); i++) {
//        	real_function_3d product = R_square*square(nemo[i]);
//        	real_function_3d potential = -1.0*J.compute_potential(R_square*square(nemo[i]));
//        	save(R*nemo[i], "phi"+stringify(i));
//        	save(product, "phi"+stringify(i)+"phi"+stringify(i));
//        	save(potential, "int_phi"+stringify(i)+"phi"+stringify(i));
//        }

        // use apply function from adiabatic correction (see AC.h, nemo.h and nemo.cc) with own potentials
        Vs = ac.apply(Vs, lra);

        save(lra, "lra_slater");
        save(Vs, "Slaterpotential");
        return Vs;

    }

    /// compute average ionization energy I like Kohut, 2014, equations (21) and (25)
    real_function_3d compute_average_I(const vecfuncT& nemo, const tensorT eigvals) const {

    	// transform 1d tensor eigvals to vector epsilon
		std::vector<double> epsilon(eigvals.size());
		for (int i = 0; i < eigvals.size(); i++) epsilon[i] = eigvals(i);

		vecfuncT nemo_square = square(world, nemo); // |nemo|^2
		scale(world, nemo_square, epsilon); // epsilon*|nemo|^2
		real_function_3d numerator = 2.0*R_square*sum(world, nemo_square); // 2 because closed shell
        real_function_3d rho = compute_density(nemo);

        // like Kohut, 2014, equations (21) and (25)
        real_function_3d I = -1.0*binary_op(numerator, rho, dens_inv(dens_thresh_lo));

//          // if tests are necessary: munge with ac
//       	real_function_3d homo_func = real_factory_3d(world).functor([] (const coord_3d& r) {return 1.0;});
//       	homo_func.scale(-1.0*eigvals(homo_ind(eigvals)));
//       	print("computing I: index of HOMO is", homo_ind(eigvals));
//       	I = ac.apply(I, homo_func);

        // munge I for long-range asymptotic behavior which is -epsilon_HOMO
       	print("computing I: index of HOMO is", homo_ind(eigvals));
       	I = binary_op(I, rho, binary_munge_linear(dens_thresh_hi, dens_thresh_lo, -1.0*eigvals(homo_ind(eigvals))));

        return I;

    }

    /// compute the kinetic energy quantity tau/rho with equation (6) from Kohut, 2014, devided by the density
    real_function_3d compute_kinetic_term(const vecfuncT& nemo, const tensorT eigvals) const {

    	// compute the denominator rho (density)
    	real_function_3d rho = compute_density(nemo);

    	// compute the numerator tau

	    // get \nabla R and (\nabla R)^2 via and U1 = -1/R \nabla R and U1dot = (1/R \nabla R)^2
    	const vecfuncT U1 = this->nuclear_correlation->U1vec();
	    NuclearCorrelationFactor::U1_dot_U1_functor u1_dot_u1(nuclear_correlation.get());
	    const real_function_3d U1dot = real_factory_3d(world).functor(u1_dot_u1).truncate_on_project();

	    // get \nabla nemo
	    std::vector<vecfuncT> grad_nemo(nemo.size());
	    for (long i = 0; i < nemo.size(); i++) {
	    	grad_nemo[i] = grad(nemo[i]);
	    }

	    // compute tau = 1/2 * sum |phi_i|^2
	    // = 1/2 * sum {(\nabla R)^2 * nemo_i^2 + 2 * R * nemo_i * (\nabla R) * (\nabla nemo_i)) + R^2 * (\nabla nemo_i)^2}
	    // = 1/2 * R^2 * sum {U1dot * nemo_i^2 + 2 * nemo_i * U1 * (\nabla nemo_i)) + (\nabla nemo_i)^2}
	    vecfuncT grad_nemo_squared(nemo.size());
		for (long i = 0; i < nemo.size(); i++) {
			grad_nemo_squared[i] = U1dot*square(nemo[i]) - 2.0*nemo[i]*dot(world, U1, grad_nemo[i])
					+ dot(world, grad_nemo[i], grad_nemo[i]);
		}
		real_function_3d tau = R_square*sum(world, grad_nemo_squared); // 1/2 * sum |Nabla nemo|^2 (* 2 because closed shell)

		// calculate quotient = tau/rho
		real_function_3d quotient = binary_op(tau, rho, dens_inv(dens_thresh_lo));

        // munge quotient for long-range asymptotic behavior which is -epsilon_HOMO
       	print("computing tau/rho: index of HOMO is", homo_ind(eigvals));
       	quotient = binary_op(quotient, rho, binary_munge_linear(dens_thresh_hi, dens_thresh_lo, -1.0*eigvals(homo_ind(eigvals))));

    	return quotient;

    }

    /// compute OCEP correction and orbital shift to add to Slater potential Vs
    real_function_3d compute_ocep_correction(const tensorT eigvalsHF, const real_function_3d IHF,
    		const vecfuncT& nemoKS, const tensorT eigvalsKS) const {

    	// Kohn-Sham average ionization energy
    	real_function_3d IKS = compute_average_I(nemoKS, eigvalsKS);

    	// calculate correction IHF - IKS like Kohut, 2014, equation (26)
       	real_function_3d correction = IHF - IKS;

    	return correction;

    }

    /// compute DCEP correction on top of the OCEP correction
    real_function_3d compute_dcep_correction(const real_function_3d kinHF, const vecfuncT& nemoKS,
    		const tensorT eigvalsKS) const {

    	// Kohn-Sham kinetic energy quantity tau/rho
    	real_function_3d kinKS = compute_kinetic_term(nemoKS, eigvalsKS);

    	// calculate correction kinHF - kinKS like Kohut, 2014, equation (33) (on top of OCEP correction)
    	real_function_3d correction = kinHF - kinKS;

    	return correction;

    }

    /// compute all potentials from given nemos except kinetic energy
    void compute_nemo_potentials(const vecfuncT& nemo, vecfuncT& Jnemo, vecfuncT& Unemo,
    		const real_function_3d V, vecfuncT& Vnemo) const {

    	// compute Coulomb part
    	Coulomb J = Coulomb(world, this);
    	Jnemo = J(nemo);
    	truncate(world, Jnemo);

    	// compute nuclear potential part
    	Nuclear Unuc(world, this->nuclear_correlation);
    	Unemo = Unuc(nemo);

    	// compute approximate OEP exchange potential part
    	Vnemo = V*nemo;

    }

    /// compute exchange potential (needed for Econv)
    void compute_exchange_potential(const vecfuncT& nemo, vecfuncT& Knemo) const {

    	Exchange K = Exchange(world, this, 0);
    	Knemo = K(nemo);
    	truncate(world, Knemo);

    }

    /// compute energy from given nemos and given OEP model for exchange
    /// for example Slater potential for OAEP
    double compute_energy(const vecfuncT phi, const vecfuncT Jphi, const real_function_3d Vx,
    		const vecfuncT Kphi, const bool vir) const {

    	// compute kinetic energy
    	// it's ok to use phi here, no regularization necessary for this eigenvalue
    	double E_kin = 0.0;
    	for (int axis = 0; axis < 3; ++axis) {
    		real_derivative_3d D = free_space_derivative<double, 3>(world, axis);
    		const vecfuncT dphi = apply(world, D, phi);
    		E_kin += 0.5 * (inner(world, dphi, dphi)).sum();
    		// -1/2 sum <Psi|Nabla^2|Psi> = 1/2 sum <NablaPsi|NablaPsi>   (integration by parts)
    	}
    	E_kin *= 2.0; // because a = b closed shell

    	// check vir: if true -> compute Evir using Levy-Perdew virial relation (Kohut_2014, (43))
    	// if false -> compute exchange energy using the expectation value of the exchange operator
    	double E_X;
    	if (vir) {
        	/// make vector of functions r = (x, y, z)
        	auto monomial_x = [] (const coord_3d& r) {return r[0];};
        	auto monomial_y = [] (const coord_3d& r) {return r[1];};
        	auto monomial_z = [] (const coord_3d& r) {return r[2];};
        	vecfuncT r(3);
        	r[0]=real_factory_3d(world).functor(monomial_x);
        	r[1]=real_factory_3d(world).functor(monomial_y);
        	r[2]=real_factory_3d(world).functor(monomial_z);
        	/// for density note that phi should be R*nemo, so no R_square is needed
        	real_function_3d rho = 2.0*dot(world, phi, phi); // 2 because closed shell
        	real_function_3d rhoterm = 3*rho + dot(world, r, grad(rho));
        	E_X = inner(Vx, rhoterm);
    	}
    	else {
    		// this uses the oep->get_calc()->amo orbitals in the current form
    		E_X = -1.0*inner(world, phi, Kphi).sum();
    	}

    	// compute external potential (nuclear attraction)
    	real_function_3d Vext = calc->potentialmanager->vnuclear();
    	const vecfuncT Vextphi = Vext*phi;

    	// compute remaining energies: nuclear attraction, Coulomb, nuclear repulsion
    	// computed as expectation values (see Szabo, Ostlund (3.81))
    	const double E_ext = 2.0 * inner(world, phi, Vextphi).sum(); // because a = b closed shell
    	const double E_J = inner(world, phi, Jphi).sum();
    	const double E_nuc = calc->molecule.nuclear_repulsion_energy();
    	double energy = E_kin + E_ext + E_J + E_X + E_nuc;

    	if (world.rank() == 0) {
    		printf("\n                       kinetic energy %15.8f Eh\n", E_kin);
    		printf("   electron-nuclear attraction energy %15.8f Eh\n", E_ext);
    		printf("                       Coulomb energy %15.8f Eh\n", E_J);
    		if (vir) printf(" exchange energy (exchange potential) %15.8f Eh\n", E_X);
    		else printf("  exchange energy (exchange operator) %15.8f Eh\n", E_X);
    		printf("     nuclear-nuclear repulsion energy %15.8f Eh\n", E_nuc);
    		printf("                         total energy %15.8f Eh\n\n", energy);
//            printf("    works for exact exchange functional only...\n");
    	}

    	return energy;

    }

};


int main(int argc, char** argv) {
    initialize(argc, argv);
    World world(SafeMPI::COMM_WORLD);
    if (world.rank() == 0) {
    	print("\n  OEP -- optimized effective potentials for DFT  \n");
    	printf("starting at time %.1f\n", wall_time());
    }
    startup(world, argc, argv);
    std::cout.precision(6);

    const std::string input = "input";
    std::shared_ptr<SCF> calc(new SCF(world, input.c_str())); /// see constructor in SCF.h

    if (world.rank() == 0) {
        calc->molecule.print();
        print("\n");
        calc->param.print(world);
    }

    std::shared_ptr<OEP> oep(new OEP(world, calc));

    vecfuncT HF_nemos;
    tensorT HF_orbens;

    /// TODO: find a way to save eigenvalues and implement restart options
//    const std::string saved_nemos = "HF_nemos";
//    const std::string saved_orbens = "HF_orbens";
//    std::ifstream f1(saved_nemos.c_str());
//    std::ifstream f2(saved_orbens.c_str());
//    if (f1.good() and f2.good()) { // if file HF_nemos and HF_orbens exist
//    	load_function(world, HF_nemos, saved_nemos);
//    	// load_tensor(... HF_orbens, saved_orbens ...);
//    }
//    else {
//    	const double energy = oep->value();
//    	HF_nemos = copy(world, oep->get_calc()->amo);
//    	HF_orbens = copy(oep->get_calc()->aeps);
//    	save_function(HF_nemos, saved_nemos);
//    	// save_tensor(... HF_orbens, saved_orbens ...);
//    }

    const double energy = oep->value();

    if (world.rank() == 0) {
        printf("final energy   %12.8f\n", energy);
        printf("finished at time %.1f\n", wall_time());
    }

    // save converged HF MOs and orbital energies
    HF_nemos = copy(world, oep->get_calc()->amo);
    HF_orbens = copy(oep->get_calc()->aeps);


//    oep->kinetic_energy_potential(oep->get_calc()->amo);
//    oep->kinetic_energy_potential2(oep->get_calc()->amo);
//    real_function_3d rhonemo = dot(world,oep->get_calc()->amo,oep->get_calc()->amo);
//    oep->make_laplacian_density_oep(rhonemo);


    // OEP model final energy
    printf("\n   +++ starting approximate OEP iterative calculation +++\n\n");

    // read additional OEP parameters from same input file used for SCF calculation (see above)
    std::ifstream in(input.c_str());
    oep->read_oep_param(in);

    oep->solve_oep(HF_nemos, HF_orbens);


    finalize();
    return 0;
}
