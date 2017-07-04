// Standard libraries
#include "headers.hpp"

// My libraries
#ifndef INCLUDE_VOLUME
	#include "volume.hpp"
	#define INCLUDE_VOLUME
#endif
#ifndef INCLUDE_UTIL
	#include "util.hpp"
	#define INCLUDE_UTIL
#endif

Volume::Volume(const std::vector< std::vector<double> > &X, const double &_depth, const std::vector<std::vector<double>::size_type> &ij, const std::vector<unsigned int> &N, const std::vector< std::vector<Condition> > &_conditions) {
	depth = &_depth;
	x = {(X[0][0] + X[0][1]) / 2, (X[1][0] + X[1][1]) / 2};
	d = {x[0] - X[0][0], x[1] - X[1][0]};
	S = {2 * d[1] * (*depth), 2 * d[0] * (*depth)};
	V = (2 * d[0]) * (2 * d[1]) * (*depth);
	
	isBoundary = false;
	conditions.resize(N.size(), std::vector<const Condition*>(N.size(), nullptr)); // {{W, E}, {S, N}}
	for (std::vector<unsigned int>::size_type i = 0; i < N.size(); i++) {
		if (ij[i] == 0) {
			conditions[i][0] = &_conditions[i][0];
			isBoundary = true;
		} else if (ij[i] == N[i]-1) {
			conditions[i][1] = &_conditions[i][1];
			isBoundary = true;
		}
	}

	// Initialize coefficients
	aP = bP = 0.0;
	a = {{0.0, 0.0}, {0.0, 0.0}};
}

// Volume::~Volume() {
// 	std::vector<const Condition*>::size_type size, sizes;
// 	sizes = conditions.size();
// 	for (std::vector<const Condition*>::size_type i = 0; i < sizes; i++) {
// 		size = conditions[i].size();
// 		for (std::vector<const Condition*>::size_type j = 0; j < size; j++) {
// 			delete conditions[i][j];
// 			delete neighbors[i][j];
// 		}
// 	}
// }

double Volume::get_depth() const {
	return (*depth);
}

std::vector<double> Volume::get_x() const {
	return x;
}

std::vector<double> Volume::get_d() const {
	return d;
}

double Volume::get_V() const {
	return V;
}

const Material* Volume::get_material() const {
	return material;
}

void Volume::set_material(const Material *_material) {
	material = _material;
}

void Volume::set_neighbors(const std::vector<std::vector<double>::size_type> &ij, const std::vector< std::vector<Volume> > &volumes) {
	neighbors.resize(conditions.size());
	for (std::vector<const Volume*>::size_type i = 0; i < neighbors.size(); i++) {
		neighbors[i].resize(conditions[i].size());
		for (std::vector<const Volume*>::size_type j = 0; j < neighbors[i].size(); j++) {
			if (conditions[i][j] == NULL) {
				if (i == 0) {
					if (j == 0) neighbors[i][j] = &(volumes[ij[0]-1][ij[1]]);
					else neighbors[i][j] = &(volumes[ij[0]+1][ij[1]]); // j == 1
				} else { // i == 1
					if (j == 0) neighbors[i][j] = &(volumes[ij[0]][ij[1]-1]);
					else neighbors[i][j] = &(volumes[ij[0]][ij[1]+1]); // j == 1
				}
			} else neighbors[i][j] = nullptr;
		}
	}
	
	// std::cout << "NEIGHBORS" << std::endl;
	// printMatrix(neighbors);
}

void Volume::computeCoefficients(const double &beta, const double &tDelta, const double &t, const double &Tprev, const std::vector< std::vector<double> > &Tneighbors) {
	// Inner volumes
	if (!isBoundary) {
		// std::cout << "INNER" << std::endl;

		aP = this->get_material()->get_rho() * this->get_material()->get_cp() * this->get_V() / tDelta;
		bP = aP * Tprev + beta * this->get_material()->get_qv() * this->get_V();

		for (std::vector<Volume>::size_type i = 0; i < neighbors.size(); i++) {
			for (std::vector<Volume>::size_type j = 0; j < neighbors[i].size(); j++) {
				a[i][j] = this->computeLambda(i, *neighbors[i][j]) * S[i] / (this->get_d()[i] + neighbors[i][j]->get_d()[i]);
				bP += (1 - beta) * a[i][j] * (Tneighbors[i][j] - Tprev);
				a[i][j] = beta * a[i][j];
				aP += a[i][j];
			}
		}
	}

	// Boundary volumes
	else {
		// std::cout << "OUTER" << std::endl;

		// Isotherm
		bP = 0.0;
		unsigned int k = 0;
		for (std::vector<Condition*>::size_type i = 0; i < conditions.size(); i++) {
			for (std::vector<Condition*>::size_type j = 0; j < conditions[i].size(); j++) {
				if (conditions[i][j] != NULL && conditions[i][j]->get_conditionType() == ISOTHERM) {
					bP += conditions[i][j]->get_T(t);
					k++;
				}
			}
		}
		if (k > 0) {
			aP = 1.0;
			bP = bP/k; // average if is a corner volume
			a = {{0.0, 0.0}, {0.0, 0.0}};
		}

		// Convection & Qflow
		else { // k == 0
			aP = this->get_material()->get_rho() * this->get_material()->get_cp() * this->get_V() / tDelta;
			bP = aP * Tprev + beta * this->get_material()->get_qv() * this->get_V();

			for (std::vector<Volume>::size_type i = 0; i < conditions.size(); i++) { // neighbors.size()
				for (std::vector<Volume>::size_type j = 0; j < conditions[i].size(); j++) { // neighbors[i].size()		// Inner side
					if (conditions[i][j] == NULL) {
						a[i][j] = this->computeLambda(i, *neighbors[i][j]) * S[i] / (this->get_d()[i] + neighbors[i][j]->get_d()[i]);
						bP += (1 - beta) * a[i][j] * (Tneighbors[i][j] - Tprev);
						a[i][j] = beta * a[i][j];
						aP += a[i][j];
					}

					// Boundary side
					else {
						a[i][j] = 0;
						if (conditions[i][j]->get_conditionType() == CONVECTION) {
							aP += beta * conditions[i][j]->get_alpha() * S[i];
							bP += beta * conditions[i][j]->get_alpha() * S[i] * conditions[i][j]->get_Tg() + (1 - beta) * conditions[i][j]->get_alpha() * (conditions[i][j]->get_Tg() - Tprev) * S[i];
						}
						else if (conditions[i][j]->get_conditionType() == FLOW) {
							// beta * Qflow + (1 - beta) * Qflow
							// bP += conditions[i][j]->get_Qflow() * this->get_d()[!i] * 2;
							bP += conditions[i][j]->get_Qflow() * this->get_depth();
						}
					}
				}
			}
		}
	}

	// std::cout << "aP = " << aP << std::endl;
	// std::cout << "bP = " << bP << std::endl;
	// printMatrix(a);
}

double Volume::computeLambda(const std::vector<Volume>::size_type &i, const Volume &neighbor) {
	return (this->get_d()[i] + neighbor.get_d()[i]) / (this->get_d()[i]/this->get_material()->get_lambda() + neighbor.get_d()[i]/neighbor.get_material()->get_lambda());
}

double Volume::get_aP() const {
	return aP;
}

double Volume::get_aW() const {
	return a[0][0];
}

double Volume::get_aE() const {
	return a[0][1];
}

double Volume::get_aS() const {
	return a[1][0];
}

double Volume::get_aN() const {
	return a[1][1];
}

double Volume::get_bP() const {
	return bP;
}
