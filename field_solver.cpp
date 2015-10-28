#include "field_solver.h"

Field_solver::Field_solver( Spatial_mesh &spat_mesh,
			    Inner_regions_manager &inner_regions,
			    Inner_regions_with_models_manager &inner_regions_with_models )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    double dx = spat_mesh.x_cell_size;
    double dy = spat_mesh.y_cell_size;
    double dz = spat_mesh.z_cell_size;
    PetscInt nrows = (nx-2)*(ny-2)*(nz-2);
    PetscInt ncols = nrows;

    PetscErrorCode ierr;
    PetscInt A_approx_nonzero_per_row = 7;

    alloc_petsc_vector( &phi_vec, nrows, "Solution" );
    ierr = VecSet( phi_vec, 0.0 ); CHKERRXX( ierr );
    alloc_petsc_vector( &rhs, nrows, "RHS" );
    alloc_petsc_matrix( &A, nrows, ncols, A_approx_nonzero_per_row );
    
    construct_equation_matrix( &A, nx, ny, nz, dx, dy, dz, inner_regions, inner_regions_with_models );
    create_solver_and_preconditioner( &ksp, &pc, &A );
}

void Field_solver::alloc_petsc_vector( Vec *x, int size, const char *name )
{
    PetscErrorCode ierr;
    ierr = VecCreate( PETSC_COMM_WORLD, x ); CHKERRXX( ierr );
    ierr = PetscObjectSetName( (PetscObject) *x, name ); CHKERRXX( ierr );
    ierr = VecSetSizes( *x, PETSC_DECIDE, size ); CHKERRXX( ierr );
    ierr = VecSetFromOptions( *x ); CHKERRXX( ierr );
    return;
}


void Field_solver::alloc_petsc_matrix( Mat *A, PetscInt nrow, PetscInt ncol, PetscInt nonzero_per_row )
{
    PetscErrorCode ierr;

    /* ierr = MatCreate( PETSC_COMM_WORLD, A ); CHKERRXX( ierr ); */
    /* ierr = MatSetSizes( *A, PETSC_DECIDE, PETSC_DECIDE, nrow, ncol ); CHKERRXX( ierr ); */
    /* ierr = MatSetFromOptions( *A ); CHKERRXX( ierr ); */
    /* ierr = MatSeqAIJSetPreallocation( *A, nonzero_per_row, NULL ); CHKERRXX( ierr ); */
    ierr = MatCreateSeqAIJ( PETSC_COMM_WORLD, nrow, ncol,
			    nonzero_per_row, NULL,  A ); CHKERRXX( ierr );
    ierr = MatSetUp( *A ); CHKERRXX( ierr );
    return;
}


void Field_solver::construct_equation_matrix( Mat *A,
					      int nx, int ny, int nz,
					      double dx, double dy, double dz,
					      Inner_regions_manager &inner_regions,
					      Inner_regions_with_models_manager &inner_regions_with_models )
{
    construct_equation_matrix_in_full_domain( A, nx, ny, nz, dx, dy, dz );
    cross_out_nodes_occupied_by_objects( A, nx, ny, nz, inner_regions, inner_regions_with_models );
    modify_equation_near_object_boundaries( A, nx, ny, nz, dx, dy, dz, inner_regions, inner_regions_with_models );
}


void Field_solver::construct_equation_matrix_in_full_domain( Mat *A,
							     int nx, int ny, int nz,
							     double dx, double dy, double dz )
{
    PetscErrorCode ierr;
    Mat d2dy2, d2dz2;
    int nrow = ( nx - 2 ) * ( ny - 2 ) * ( nz - 2 );
    int ncol = nrow;
    PetscInt nonzero_per_row = 7; // approx

    construct_d2dx2_in_3d( A, nx, ny, nz );
    ierr = MatScale( *A, dy * dy * dz * dz ); CHKERRXX( ierr );
    
    alloc_petsc_matrix( &d2dy2, nrow, ncol, nonzero_per_row );
    construct_d2dy2_in_3d( &d2dy2, nx, ny, nz );
    ierr = MatAXPY( *A, dx * dx * dz * dz, d2dy2, DIFFERENT_NONZERO_PATTERN ); CHKERRXX( ierr );
    ierr = MatDestroy( &d2dy2 ); CHKERRXX( ierr );

    alloc_petsc_matrix( &d2dz2, nrow, ncol, nonzero_per_row );
    construct_d2dz2_in_3d( &d2dz2, nx, ny, nz );
    ierr = MatAXPY( *A, dx * dx * dy * dy, d2dz2, DIFFERENT_NONZERO_PATTERN ); CHKERRXX( ierr );
    ierr = MatDestroy( &d2dz2 ); CHKERRXX( ierr );

    return;
}

void Field_solver::cross_out_nodes_occupied_by_objects( Mat *A,
							int nx, int ny, int nz,
							Inner_regions_manager &inner_regions,
							Inner_regions_with_models_manager &inner_regions_with_models )
{
    for( auto &reg : inner_regions.regions )
	cross_out_nodes_occupied_by_objects( A, nx, ny, nz, reg );
    for( auto &reg : inner_regions_with_models.regions )
	cross_out_nodes_occupied_by_objects( A, nx, ny, nz, reg );    
}


void Field_solver::cross_out_nodes_occupied_by_objects( Mat *A,
							int nx, int ny, int nz,
							Inner_region &inner_region )
{
    std::vector<int> occupied_nodes_global_indices =
	list_of_nodes_global_indices_in_matrix( inner_region.inner_nodes_not_at_domain_edge, nx, ny, nz );
    
    PetscErrorCode ierr;
    PetscInt num_of_rows_to_remove = occupied_nodes_global_indices.size();
    if( num_of_rows_to_remove != 0 ){
	PetscInt *rows_global_indices = &occupied_nodes_global_indices[0];
	PetscScalar diag = 1.0;
	PetscScalar charge_density_inside_conductor = 0.0;
	Vec phi_inside_region, rhs_inside_region; /* Approx solution and RHS at zeroed rows */

	// looks like it doesn't work
	
	// todo: separate function
	std::string vec_name = "Phi inside " + inner_region.name;
	alloc_petsc_vector( &phi_inside_region,
			    (nx-2) * (ny-2) * (nz-2),
			    vec_name.c_str() );
	VecSet( phi_inside_region, inner_region.potential );    
	ierr = VecAssemblyBegin( phi_inside_region ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( phi_inside_region ); CHKERRXX( ierr );

	// todo: separate function
	vec_name = "RHS inside " + inner_region.name;
	alloc_petsc_vector( &rhs_inside_region,
			    (nx-2) * (ny-2) * (nz-2),
			    vec_name.c_str() );
	VecSet( rhs_inside_region, charge_density_inside_conductor );    
	ierr = VecAssemblyBegin( rhs_inside_region ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( rhs_inside_region ); CHKERRXX( ierr );
	
	ierr = MatZeroRows( *A, num_of_rows_to_remove, rows_global_indices,
			    diag, phi_inside_region, rhs_inside_region); CHKERRXX( ierr );

	// ierr = VecDestroy( &phi_inside_region ); CHKERRXX( ierr );
	// ierr = VecDestroy( &rhs_inside_region ); CHKERRXX( ierr );
	// todo: without vecdestroy call there is memory leak	
    }
}

void Field_solver::cross_out_nodes_occupied_by_objects( Mat *A,
							int nx, int ny, int nz,
							Inner_region_with_model &inner_region )
{
    std::vector<int> occupied_nodes_global_indices =
	list_of_nodes_global_indices_in_matrix( inner_region.inner_nodes_not_at_domain_edge, nx, ny, nz );
    
    PetscErrorCode ierr;
    PetscInt num_of_rows_to_remove = occupied_nodes_global_indices.size();
    if( num_of_rows_to_remove != 0 ){
	PetscInt *rows_global_indices = &occupied_nodes_global_indices[0];
	PetscScalar diag = 1.0;
	PetscScalar charge_density_inside_conductor = 0.0;
	Vec phi_inside_region, rhs_inside_region; /* Approx solution and RHS at zeroed rows */

	// looks like it doesn't work
	
	// todo: separate function
	std::string vec_name = "Phi inside " + inner_region.name;
	alloc_petsc_vector( &phi_inside_region,
			    (nx-2) * (ny-2) * (nz-2),
			    vec_name.c_str() );
	VecSet( phi_inside_region, inner_region.potential );    
	ierr = VecAssemblyBegin( phi_inside_region ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( phi_inside_region ); CHKERRXX( ierr );

	// todo: separate function
	vec_name = "RHS inside " + inner_region.name;
	alloc_petsc_vector( &rhs_inside_region,
			    (nx-2) * (ny-2) * (nz-2),
			    vec_name.c_str() );
	VecSet( rhs_inside_region, charge_density_inside_conductor );    
	ierr = VecAssemblyBegin( rhs_inside_region ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( rhs_inside_region ); CHKERRXX( ierr );
	
	ierr = MatZeroRows( *A, num_of_rows_to_remove, rows_global_indices,
			    diag, phi_inside_region, rhs_inside_region); CHKERRXX( ierr );

	// ierr = VecDestroy( &phi_inside_region ); CHKERRXX( ierr );
	// ierr = VecDestroy( &rhs_inside_region ); CHKERRXX( ierr );
	// todo: without vecdestroy call there is memory leak	
    }
}


void Field_solver::modify_equation_near_object_boundaries( Mat *A,
							   int nx, int ny, int nz,
							   double dx, double dy, double dz,
							   Inner_regions_manager &inner_regions,
							   Inner_regions_with_models_manager &inner_regions_with_models )
{
    for( auto &reg : inner_regions.regions )
	modify_equation_near_object_boundaries( A, nx, ny, nz, dx, dy, dz, reg );
    for( auto &reg : inner_regions_with_models.regions )
	modify_equation_near_object_boundaries( A, nx, ny, nz, dx, dy, dz, reg );

}


void Field_solver::modify_equation_near_object_boundaries( Mat *A,
							   int nx, int ny, int nz,
							   double dx, double dy, double dz,
							   Inner_region &inner_region )
{
    PetscErrorCode ierr;
    int max_possible_neighbours = 6; // in 3d case; todo: make max_possible_nbr a property of Node_reference
    PetscScalar zeroes[max_possible_neighbours] = { 0.0 };
    
    for( auto &node : inner_region.near_boundary_nodes_not_at_domain_edge ){
	PetscInt modify_single_row = 1;
	PetscInt row_to_modify = node_global_index_in_matrix( node, nx, ny, nz );
	std::vector<PetscInt> cols_to_modify =
	    adjacent_nodes_not_at_domain_edge_and_inside_inner_region( node, inner_region,
								       nx, ny, nz, dx, dy, dz );
	PetscInt n_of_cols_to_modify = cols_to_modify.size();
	
	if( n_of_cols_to_modify != 0 ){
	    PetscInt *col_indices = &cols_to_modify[0];
	    ierr = MatSetValues( *A,
				 modify_single_row, &row_to_modify,
				 n_of_cols_to_modify, col_indices,
				 zeroes, INSERT_VALUES );
	    CHKERRXX( ierr );	
	}
    }
    
    ierr = MatAssemblyBegin( *A, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    ierr = MatAssemblyEnd( *A, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
}

void Field_solver::modify_equation_near_object_boundaries( Mat *A,
							   int nx, int ny, int nz,
							   double dx, double dy, double dz,
							   Inner_region_with_model &inner_region )
{
    PetscErrorCode ierr;
    int max_possible_neighbours = 6; // in 3d case; todo: make max_possible_nbr a property of Node_reference
    PetscScalar zeroes[max_possible_neighbours] = { 0.0 };
    
    for( auto &node : inner_region.near_boundary_nodes_not_at_domain_edge ){
	PetscInt modify_single_row = 1;
	PetscInt row_to_modify = node_global_index_in_matrix( node, nx, ny, nz );
	std::vector<PetscInt> cols_to_modify =
	    adjacent_nodes_not_at_domain_edge_and_inside_inner_region( node, inner_region,
								       nx, ny, nz, dx, dy, dz );
	PetscInt n_of_cols_to_modify = cols_to_modify.size();
	
	if( n_of_cols_to_modify != 0 ){
	    PetscInt *col_indices = &cols_to_modify[0];
	    ierr = MatSetValues( *A,
				 modify_single_row, &row_to_modify,
				 n_of_cols_to_modify, col_indices,
				 zeroes, INSERT_VALUES );
	    CHKERRXX( ierr );	
	}
    }
    
    ierr = MatAssemblyBegin( *A, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    ierr = MatAssemblyEnd( *A, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
}


std::vector<PetscInt> Field_solver::adjacent_nodes_not_at_domain_edge_and_inside_inner_region(
    Node_reference &node,
    Inner_region &inner_region,
    int nx, int ny, int nz,
    double dx, double dy, double dz )
{
    int max_possible_neighbours = 6; // in 3d case; todo: make max_possible_nbr a property of Node_reference
    std::vector<PetscInt> resulting_global_indices;
    resulting_global_indices.reserve( max_possible_neighbours );
    for( auto &adj_node : node.adjacent_nodes() ){
	if( !adj_node.at_domain_edge( nx, ny, nz ) &&
	    inner_region.check_if_node_inside( adj_node, dx, dy, dz ) ){
	    resulting_global_indices.push_back( node_global_index_in_matrix( adj_node, nx, ny, nz ) );
	}
    }
    return resulting_global_indices;
}

std::vector<PetscInt> Field_solver::adjacent_nodes_not_at_domain_edge_and_inside_inner_region(
    Node_reference &node,
    Inner_region_with_model &inner_region,
    int nx, int ny, int nz,
    double dx, double dy, double dz )
{
    int max_possible_neighbours = 6; // in 3d case; todo: make max_possible_nbr a property of Node_reference
    std::vector<PetscInt> resulting_global_indices;
    resulting_global_indices.reserve( max_possible_neighbours );
    for( auto &adj_node : node.adjacent_nodes() ){
	if( !adj_node.at_domain_edge( nx, ny, nz ) &&
	    inner_region.check_if_node_inside( adj_node, dx, dy, dz ) ){
	    resulting_global_indices.push_back( node_global_index_in_matrix( adj_node, nx, ny, nz ) );
	}
    }
    return resulting_global_indices;
}


void Field_solver::construct_d2dx2_in_3d( Mat *d2dx2_3d, int nx, int ny, int nz )
{    
    PetscErrorCode ierr;
    Mat d2dx2_2d;
    int nrow_2d = ( nx - 2 ) * ( ny - 2 );
    int ncol_2d = nrow_2d;
    PetscInt nonzero_per_row = 5; // approx

    alloc_petsc_matrix( &d2dx2_2d, nrow_2d, ncol_2d, nonzero_per_row );
    construct_d2dx2_in_2d( &d2dx2_2d, nx, ny );
    multiply_pattern_along_diagonal( d2dx2_3d, &d2dx2_2d, (nx-2)*(ny-2), nz-2 );
    ierr = MatDestroy( &d2dx2_2d ); CHKERRXX( ierr );

    return;
}

void Field_solver::construct_d2dy2_in_3d( Mat *d2dy2_3d, int nx, int ny, int nz )
{
    PetscErrorCode ierr;
    Mat d2dy2_2d;
    int nrow_2d = ( nx - 2 ) * ( ny - 2 );
    int ncol_2d = nrow_2d;
    PetscInt nonzero_per_row = 5; // approx

    alloc_petsc_matrix( &d2dy2_2d, nrow_2d, ncol_2d, nonzero_per_row );
    construct_d2dy2_in_2d( &d2dy2_2d, nx, ny );
    multiply_pattern_along_diagonal( d2dy2_3d, &d2dy2_2d, (nx-2)*(ny-2), nz-2 );
    ierr = MatDestroy( &d2dy2_2d ); CHKERRXX( ierr );

    return;
}

void Field_solver::construct_d2dz2_in_3d( Mat *d2dz2_3d, int nx, int ny, int nz )
{
    PetscErrorCode ierr;    
    int nrow = ( nx - 2 ) * ( ny - 2 ) * ( nz - 2 );
    //int ncol = nrow;
    int at_boundary_pattern_size = 2;
    int no_boundaries_pattern_size = 3;
    PetscScalar at_near_boundary_pattern[at_boundary_pattern_size];
    PetscScalar no_boundaries_pattern[no_boundaries_pattern_size];
    PetscScalar at_far_boundary_pattern[at_boundary_pattern_size];
    PetscInt cols[ no_boundaries_pattern_size ]; 
    at_near_boundary_pattern[0] = -2.0;
    at_near_boundary_pattern[1] = 1.0;
    no_boundaries_pattern[0] = 1.0;
    no_boundaries_pattern[1] = -2.0;
    no_boundaries_pattern[2] = 1.0;
    at_far_boundary_pattern[0] = 1.0;
    at_far_boundary_pattern[1] = -2.0;
  
    for( int i = 0; i < nrow; i++ ) {	
	if ( i < ( nx - 2 ) * ( ny - 2 ) ) {
	    // near boundary
	    cols[0] = i;
	    cols[1] = i + ( nx - 2 ) * ( ny - 2 );
	    ierr = MatSetValues( *d2dz2_3d,
				 1, &i,
				 at_boundary_pattern_size, cols,
				 at_near_boundary_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	} else if ( i >= ( nx - 2 ) * ( ny - 2 ) * ( nz - 3 ) ) {
	    // far boundary
	    cols[0] = i - ( nx - 2 ) * ( ny - 2 );
	    cols[1] = i;
	    ierr = MatSetValues( *d2dz2_3d,
				 1, &i,
				 at_boundary_pattern_size, cols,
				 at_far_boundary_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	} else {
	    // center
	    cols[0] = i - ( nx - 2 ) * ( ny - 2 );
	    cols[1] = i;
	    cols[2] = i + ( nx - 2 ) * ( ny - 2 );
	    ierr = MatSetValues( *d2dz2_3d,
				 1, &i,
				 no_boundaries_pattern_size, cols, 
				 no_boundaries_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	}
    }
    
    ierr = MatAssemblyBegin( *d2dz2_3d, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    ierr = MatAssemblyEnd( *d2dz2_3d, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    return;
}


void Field_solver::multiply_pattern_along_diagonal( Mat *result, Mat *pattern, int pt_size, int n_times )
{
    PetscErrorCode ierr;    
    int mul_nrow = pt_size * n_times;
    //int mul_ncol = mul_nrow;
    int pattern_i;
    // int pattern_j;
    PetscInt pattern_nonzero_cols_number;
    const PetscInt *pattern_nonzero_cols;
    const PetscScalar *pattern_nonzero_vals;
    
    for( int i = 0; i < mul_nrow; i++ ) {
	pattern_i = i%pt_size;
	ierr = MatGetRow( *pattern,
			  pattern_i, &pattern_nonzero_cols_number, &pattern_nonzero_cols,
			  &pattern_nonzero_vals); CHKERRXX( ierr );

	PetscInt result_nonzero_cols[pattern_nonzero_cols_number];

	for( int t = 0; t < pattern_nonzero_cols_number; t++  ){
	    result_nonzero_cols[t] = pattern_nonzero_cols[t] + ( i / pt_size ) * pt_size;
	}

	ierr = MatSetValues( *result,
			     1, &i,
			     pattern_nonzero_cols_number, result_nonzero_cols,
			     pattern_nonzero_vals, INSERT_VALUES); CHKERRXX( ierr );
	MatRestoreRow( *pattern,
		       pattern_i, &pattern_nonzero_cols_number, &pattern_nonzero_cols,
		       &pattern_nonzero_vals ); CHKERRXX( ierr );
    }

    ierr = MatAssemblyBegin( *result, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    ierr = MatAssemblyEnd( *result, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    
    return;
}


void Field_solver::construct_d2dx2_in_2d( Mat *d2dx2_2d, int nx, int ny )
{
    PetscErrorCode ierr;
    int nrow = ( nx - 2 ) * ( ny - 2 );
    //int ncol = nrow;
    int at_boundary_pattern_size = 2;
    int no_boundaries_pattern_size = 3;
    PetscScalar at_left_boundary_pattern[at_boundary_pattern_size];
    PetscScalar no_boundaries_pattern[no_boundaries_pattern_size];
    PetscScalar at_right_boundary_pattern[at_boundary_pattern_size];
    PetscInt cols[ no_boundaries_pattern_size ]; 
    at_left_boundary_pattern[0] = -2.0;
    at_left_boundary_pattern[1] = 1.0;
    no_boundaries_pattern[0] = 1.0;
    no_boundaries_pattern[1] = -2.0;
    no_boundaries_pattern[2] = 1.0;
    at_right_boundary_pattern[0] = 1.0;
    at_right_boundary_pattern[1] = -2.0;
    
    for( int i = 0; i < nrow; i++ ) {	
	if ( (i + 1) % ( nx - 2 ) == 1 ) {
	    // left boundary
	    cols[0] = i;
	    cols[1] = i+1;
	    ierr = MatSetValues( *d2dx2_2d,
				 1, &i,
				 at_boundary_pattern_size, cols,
				 at_left_boundary_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	} else if ( (i + 1) % ( nx - 2 ) == 0 ) {
	    // right boundary
	    cols[0] = i-1;
	    cols[1] = i;
	    ierr = MatSetValues( *d2dx2_2d,
				 1, &i,
				 at_boundary_pattern_size, cols,
				 at_right_boundary_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	} else {
	    // center
	    cols[0] = i-1;
	    cols[1] = i;
	    cols[2] = i+1;
	    ierr = MatSetValues( *d2dx2_2d,
				 1, &i,
				 no_boundaries_pattern_size, cols, 
				 no_boundaries_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	}
    }		
    
    ierr = MatAssemblyBegin( *d2dx2_2d, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    ierr = MatAssemblyEnd( *d2dx2_2d, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    return;
}

void Field_solver::construct_d2dy2_in_2d( Mat *d2dy2_2d, int nx, int ny )
{
    PetscErrorCode ierr;    
    int nrow = ( nx - 2 ) * ( ny - 2 );
    //int ncol = nrow;
    int at_boundary_pattern_size = 2;
    int no_boundaries_pattern_size = 3;
    PetscScalar at_top_boundary_pattern[at_boundary_pattern_size];
    PetscScalar no_boundaries_pattern[no_boundaries_pattern_size];
    PetscScalar at_bottom_boundary_pattern[at_boundary_pattern_size];
    PetscInt cols[ no_boundaries_pattern_size ]; 
    at_bottom_boundary_pattern[0] = -2.0;
    at_bottom_boundary_pattern[1] = 1.0;
    no_boundaries_pattern[0] = 1.0;
    no_boundaries_pattern[1] = -2.0;
    no_boundaries_pattern[2] = 1.0;
    at_top_boundary_pattern[0] = 1.0;
    at_top_boundary_pattern[1] = -2.0;
  
    for( int i = 0; i < nrow; i++ ) {	
	if ( i < nx - 2 ) {
	    // bottom boundary
	    cols[0] = i;
	    cols[1] = i + ( nx - 2 );
	    ierr = MatSetValues( *d2dy2_2d,
				 1, &i,
				 at_boundary_pattern_size, cols,
				 at_bottom_boundary_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	} else if ( i >= ( nx - 2 ) * ( ny - 3 ) ) {
	    // top boundary
	    cols[0] = i - ( nx - 2 );
	    cols[1] = i;
	    ierr = MatSetValues( *d2dy2_2d,
				 1, &i,
				 at_boundary_pattern_size, cols,
				 at_top_boundary_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	} else {
	    // center
	    cols[0] = i - ( nx - 2 );
	    cols[1] = i;
	    cols[2] = i + ( nx - 2 );
	    ierr = MatSetValues( *d2dy2_2d,
				 1, &i,
				 no_boundaries_pattern_size, cols, 
				 no_boundaries_pattern, INSERT_VALUES );
	    CHKERRXX( ierr );
	}
    }		
    
    ierr = MatAssemblyBegin( *d2dy2_2d, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    ierr = MatAssemblyEnd( *d2dy2_2d, MAT_FINAL_ASSEMBLY ); CHKERRXX( ierr );
    return;
}

void Field_solver::create_solver_and_preconditioner( KSP *ksp, PC *pc, Mat *A )
{
    PetscReal rtol = 1.e-12;
    // Default.     
    // Possible to specify from command line using '-ksp_rtol' option.
    
    PetscErrorCode ierr;
    ierr = KSPCreate( PETSC_COMM_WORLD, ksp ); CHKERRXX(ierr);
    ierr = KSPSetOperators( *ksp, *A, *A, DIFFERENT_NONZERO_PATTERN ); CHKERRXX(ierr);
    //ierr = KSPSetOperators( *ksp, *A, *A ); CHKERRXX(ierr);
    ierr = KSPGetPC( *ksp, pc ); CHKERRXX(ierr);
    ierr = PCSetType( *pc, PCGAMG ); CHKERRXX(ierr);
    ierr = KSPSetType( *ksp, KSPGMRES ); CHKERRXX(ierr);
    ierr = KSPSetTolerances( *ksp, rtol,
			     PETSC_DEFAULT, PETSC_DEFAULT, PETSC_DEFAULT); CHKERRXX(ierr);
    ierr = KSPSetFromOptions( *ksp ); CHKERRXX(ierr);    
    ierr = KSPSetInitialGuessNonzero( *ksp, PETSC_TRUE ); CHKERRXX( ierr );

    ierr = KSPSetUp( *ksp ); CHKERRXX(ierr);
    return;
}

void Field_solver::eval_potential( Spatial_mesh &spat_mesh,
				   Inner_regions_manager &inner_regions,
				   Inner_regions_with_models_manager &inner_regions_with_models )
{
    solve_poisson_eqn( spat_mesh, inner_regions, inner_regions_with_models );
}

void Field_solver::solve_poisson_eqn( Spatial_mesh &spat_mesh,
				      Inner_regions_manager &inner_regions,
				      Inner_regions_with_models_manager &inner_regions_with_models )
{
    PetscErrorCode ierr;

    init_rhs_vector( spat_mesh, inner_regions, inner_regions_with_models );
    ierr = KSPSolve( ksp, rhs, phi_vec); CHKERRXX( ierr );

    // This should be done in 'cross_out_nodes_occupied_by_objects' by
    // MatZeroRows function but it seems it doesn't work
    set_solution_at_nodes_of_inner_regions( spat_mesh, inner_regions, inner_regions_with_models );

    transfer_solution_to_spat_mesh( spat_mesh );
    return;
}

void Field_solver::init_rhs_vector( Spatial_mesh &spat_mesh,
				    Inner_regions_manager &inner_regions,
				    Inner_regions_with_models_manager &inner_regions_with_models )
{
    init_rhs_vector_in_full_domain( spat_mesh );
    set_rhs_at_nodes_occupied_by_objects( spat_mesh, inner_regions, inner_regions_with_models );
    modify_rhs_near_object_boundaries( spat_mesh, inner_regions, inner_regions_with_models );
}

void Field_solver::init_rhs_vector_in_full_domain( Spatial_mesh &spat_mesh )
{
    PetscErrorCode ierr;
    
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    //int nrow = (nx-2)*(ny-2);
    double dx = spat_mesh.x_cell_size;
    double dy = spat_mesh.y_cell_size;
    double dz = spat_mesh.z_cell_size;
    double rhs_at_node;

    // todo: split into separate functions
    // start processing rho from the near top left corner
    for ( int k = 1; k <= nz-2; k++ ) {
	for ( int j = 1; j <= ny-2; j++ ) { 
	    for ( int i = 1; i <= nx-2; i++ ) {
		// - 4 * pi * rho * dx^2 * dy^2
		rhs_at_node = -4.0 * M_PI * spat_mesh.charge_density[i][j][k];
		rhs_at_node = rhs_at_node * dx * dx * dy * dy * dz * dz;
		// left and right boundary
		rhs_at_node = rhs_at_node
		    - dy * dy * dz * dz *
		    ( kronecker_delta(i,1) * spat_mesh.potential[0][j][k] +
		      kronecker_delta(i,nx-2) * spat_mesh.potential[nx-1][j][k] );
		// top and bottom boundary
		rhs_at_node = rhs_at_node
		    - dx * dx * dz * dz *
		    ( kronecker_delta(j,1) * spat_mesh.potential[i][0][k] +
		      kronecker_delta(j,ny-2) * spat_mesh.potential[i][ny-1][k] );
		// near and far boundary
		rhs_at_node = rhs_at_node
		    - dx * dx * dy * dy *
		    ( kronecker_delta(k,1) * spat_mesh.potential[i][j][0] +
		      kronecker_delta(k,nz-2) * spat_mesh.potential[i][j][nz-1] );
		// set rhs vector values
		ierr = VecSetValue( rhs,
				    node_ijk_to_global_index_in_matrix( i, j, k, nx, ny, nz ),
				    rhs_at_node, INSERT_VALUES );
		CHKERRXX( ierr );
	    }
	}
    }
    
    ierr = VecAssemblyBegin( rhs ); CHKERRXX( ierr );
    ierr = VecAssemblyEnd( rhs ); CHKERRXX( ierr );
    
    return;
}

void Field_solver::set_rhs_at_nodes_occupied_by_objects( Spatial_mesh &spat_mesh,
							 Inner_regions_manager &inner_regions,
							 Inner_regions_with_models_manager &inner_regions_with_models )
{
    for( auto &reg : inner_regions.regions )
	set_rhs_at_nodes_occupied_by_objects( spat_mesh, reg );
    for( auto &reg : inner_regions_with_models.regions )
	set_rhs_at_nodes_occupied_by_objects( spat_mesh, reg );

}

void Field_solver::set_rhs_at_nodes_occupied_by_objects( Spatial_mesh &spat_mesh,
							 Inner_region &inner_region )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;

    std::vector<PetscInt> indices_of_inner_nodes_not_at_domain_edge;
    indices_of_inner_nodes_not_at_domain_edge =
	list_of_nodes_global_indices_in_matrix( inner_region.inner_nodes_not_at_domain_edge, nx, ny, nz );

    PetscErrorCode ierr;
    PetscInt num_of_elements = indices_of_inner_nodes_not_at_domain_edge.size();
    if( num_of_elements != 0 ){
	PetscInt *global_indices = &indices_of_inner_nodes_not_at_domain_edge[0];
	PetscScalar zeroes[num_of_elements] = {0.0};
    
	ierr = VecSetValues( rhs, num_of_elements, global_indices, zeroes, INSERT_VALUES );
	CHKERRXX( ierr );

	ierr = VecAssemblyBegin( rhs ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( rhs ); CHKERRXX( ierr );
    }
}

void Field_solver::set_rhs_at_nodes_occupied_by_objects( Spatial_mesh &spat_mesh,
							 Inner_region_with_model &inner_region )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;

    std::vector<PetscInt> indices_of_inner_nodes_not_at_domain_edge;
    indices_of_inner_nodes_not_at_domain_edge =
	list_of_nodes_global_indices_in_matrix( inner_region.inner_nodes_not_at_domain_edge, nx, ny, nz );

    PetscErrorCode ierr;
    PetscInt num_of_elements = indices_of_inner_nodes_not_at_domain_edge.size();
    if( num_of_elements != 0 ){
	PetscInt *global_indices = &indices_of_inner_nodes_not_at_domain_edge[0];
	PetscScalar zeroes[num_of_elements] = {0.0};
    
	ierr = VecSetValues( rhs, num_of_elements, global_indices, zeroes, INSERT_VALUES );
	CHKERRXX( ierr );

	ierr = VecAssemblyBegin( rhs ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( rhs ); CHKERRXX( ierr );
    }
}


void Field_solver::modify_rhs_near_object_boundaries( Spatial_mesh &spat_mesh,
						      Inner_regions_manager &inner_regions,
						      Inner_regions_with_models_manager &inner_regions_with_models )
{
    for( auto &reg : inner_regions.regions )
	modify_rhs_near_object_boundaries( spat_mesh, reg );
    for( auto &reg : inner_regions_with_models.regions )
	modify_rhs_near_object_boundaries( spat_mesh, reg );

}


void Field_solver::modify_rhs_near_object_boundaries( Spatial_mesh &spat_mesh,
						      Inner_region &inner_region )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    //int nrow = (nx-2)*(ny-2);
    double dx = spat_mesh.x_cell_size;
    double dy = spat_mesh.y_cell_size;
    double dz = spat_mesh.z_cell_size;

    PetscErrorCode ierr;    
    std::vector<PetscInt> indices_of_nodes_near_boundaries;
    std::vector<PetscScalar> rhs_modification_for_nodes_near_boundaries;

    indicies_of_near_boundary_nodes_and_rhs_modifications(
	indices_of_nodes_near_boundaries,
	rhs_modification_for_nodes_near_boundaries,
	nx, ny, nz,
	dx, dy, dz,
	inner_region );
    
    PetscInt number_of_elements = indices_of_nodes_near_boundaries.size();
    if( number_of_elements != 0 ){
	PetscInt *indices = &indices_of_nodes_near_boundaries[0];
	PetscScalar *values = &rhs_modification_for_nodes_near_boundaries[0];
	ierr = VecSetValues( rhs, number_of_elements,
			     indices, values, ADD_VALUES ); CHKERRXX( ierr );
	CHKERRXX( ierr );
	
	ierr = VecAssemblyBegin( rhs ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( rhs ); CHKERRXX( ierr );
    }
}

void Field_solver::modify_rhs_near_object_boundaries( Spatial_mesh &spat_mesh,
						      Inner_region_with_model &inner_region )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    //int nrow = (nx-2)*(ny-2);
    double dx = spat_mesh.x_cell_size;
    double dy = spat_mesh.y_cell_size;
    double dz = spat_mesh.z_cell_size;

    PetscErrorCode ierr;    
    std::vector<PetscInt> indices_of_nodes_near_boundaries;
    std::vector<PetscScalar> rhs_modification_for_nodes_near_boundaries;

    indicies_of_near_boundary_nodes_and_rhs_modifications(
	indices_of_nodes_near_boundaries,
	rhs_modification_for_nodes_near_boundaries,
	nx, ny, nz,
	dx, dy, dz,
	inner_region );
    
    PetscInt number_of_elements = indices_of_nodes_near_boundaries.size();
    if( number_of_elements != 0 ){
	PetscInt *indices = &indices_of_nodes_near_boundaries[0];
	PetscScalar *values = &rhs_modification_for_nodes_near_boundaries[0];
	ierr = VecSetValues( rhs, number_of_elements,
			     indices, values, ADD_VALUES ); CHKERRXX( ierr );
	CHKERRXX( ierr );
	
	ierr = VecAssemblyBegin( rhs ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( rhs ); CHKERRXX( ierr );
    }
}


void Field_solver::indicies_of_near_boundary_nodes_and_rhs_modifications(
    std::vector<PetscInt> &indices_of_nodes_near_boundaries,
    std::vector<PetscScalar> &rhs_modification_for_nodes_near_boundaries,
    int nx, int ny, int nz,
    double dx, double dy, double dz,
    Inner_region &inner_region )
{
    int max_possible_nodes_where_to_modify_rhs = inner_region.near_boundary_nodes_not_at_domain_edge.size();
    indices_of_nodes_near_boundaries.reserve( max_possible_nodes_where_to_modify_rhs );
    rhs_modification_for_nodes_near_boundaries.reserve( max_possible_nodes_where_to_modify_rhs );
    
    for( auto &node : inner_region.near_boundary_nodes_not_at_domain_edge ){
	PetscScalar rhs_mod = 0.0;
	for( auto &adj_node : node.adjacent_nodes() ){
	    // possible todo: separate function for rhs_mod evaluation?
	    if( !adj_node.at_domain_edge( nx, ny, nz ) &&
		inner_region.check_if_node_inside( adj_node, dx, dy, dz ) ){
		if( adj_node.left_from( node ) ) {
		    rhs_mod += -inner_region.potential * dy * dy * dz * dz;
		} else if( adj_node.right_from( node ) ) {
		    rhs_mod += -inner_region.potential * dy * dy * dz * dz;
		} else if( adj_node.top_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dz * dz;
		} else if( adj_node.bottom_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dz * dz;
		} else if( adj_node.near_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dy * dy;
		} else if( adj_node.far_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dy * dy;
		}
	    }
	}
	indices_of_nodes_near_boundaries.push_back( node_global_index_in_matrix( node, nx, ny, nz ) );
	rhs_modification_for_nodes_near_boundaries.push_back( rhs_mod );
    }
}

void Field_solver::indicies_of_near_boundary_nodes_and_rhs_modifications(
    std::vector<PetscInt> &indices_of_nodes_near_boundaries,
    std::vector<PetscScalar> &rhs_modification_for_nodes_near_boundaries,
    int nx, int ny, int nz,
    double dx, double dy, double dz,
    Inner_region_with_model &inner_region )
{
    int max_possible_nodes_where_to_modify_rhs = inner_region.near_boundary_nodes_not_at_domain_edge.size();
    indices_of_nodes_near_boundaries.reserve( max_possible_nodes_where_to_modify_rhs );
    rhs_modification_for_nodes_near_boundaries.reserve( max_possible_nodes_where_to_modify_rhs );
    
    for( auto &node : inner_region.near_boundary_nodes_not_at_domain_edge ){
	PetscScalar rhs_mod = 0.0;
	for( auto &adj_node : node.adjacent_nodes() ){
	    // possible todo: separate function for rhs_mod evaluation?
	    if( !adj_node.at_domain_edge( nx, ny, nz ) &&
		inner_region.check_if_node_inside( adj_node, dx, dy, dz ) ){
		if( adj_node.left_from( node ) ) {
		    rhs_mod += -inner_region.potential * dy * dy * dz * dz;
		} else if( adj_node.right_from( node ) ) {
		    rhs_mod += -inner_region.potential * dy * dy * dz * dz;
		} else if( adj_node.top_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dz * dz;
		} else if( adj_node.bottom_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dz * dz;
		} else if( adj_node.near_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dy * dy;
		} else if( adj_node.far_from( node ) ) {
		    rhs_mod += -inner_region.potential * dx * dx * dy * dy;
		}
	    }
	}
	indices_of_nodes_near_boundaries.push_back( node_global_index_in_matrix( node, nx, ny, nz ) );
	rhs_modification_for_nodes_near_boundaries.push_back( rhs_mod );
    }
}


void Field_solver::set_solution_at_nodes_of_inner_regions( Spatial_mesh &spat_mesh,
							   Inner_regions_manager &inner_regions,
							   Inner_regions_with_models_manager &inner_regions_with_models ) 
{
    for( auto &reg : inner_regions.regions )
	set_solution_at_nodes_of_inner_regions( spat_mesh, reg );
    for( auto &reg : inner_regions_with_models.regions )
	set_solution_at_nodes_of_inner_regions( spat_mesh, reg );

}

void Field_solver::set_solution_at_nodes_of_inner_regions( Spatial_mesh &spat_mesh,
							   Inner_region &inner_region )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    
    std::vector<int> occupied_nodes_global_indices =
	list_of_nodes_global_indices_in_matrix( inner_region.inner_nodes_not_at_domain_edge, nx, ny, nz );
    
    PetscErrorCode ierr;
    PetscInt num_of_elements_to_set = occupied_nodes_global_indices.size();
    if( num_of_elements_to_set != 0 ){
	std::vector<PetscScalar> phi_inside_region(num_of_elements_to_set);
	std::fill( phi_inside_region.begin(), phi_inside_region.end(), inner_region.potential );
	
	PetscInt *global_indices = &occupied_nodes_global_indices[0];
	PetscScalar *values = &phi_inside_region[0];
	
	ierr = VecSetValues( phi_vec, num_of_elements_to_set, global_indices,
			     values, INSERT_VALUES); CHKERRXX( ierr );
	ierr = VecAssemblyBegin( phi_vec ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( phi_vec ); CHKERRXX( ierr );
    }
    
    return;
}

void Field_solver::set_solution_at_nodes_of_inner_regions( Spatial_mesh &spat_mesh,
							   Inner_region_with_model &inner_region )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    
    std::vector<int> occupied_nodes_global_indices =
	list_of_nodes_global_indices_in_matrix( inner_region.inner_nodes_not_at_domain_edge, nx, ny, nz );
    
    PetscErrorCode ierr;
    PetscInt num_of_elements_to_set = occupied_nodes_global_indices.size();
    if( num_of_elements_to_set != 0 ){
	std::vector<PetscScalar> phi_inside_region(num_of_elements_to_set);
	std::fill( phi_inside_region.begin(), phi_inside_region.end(), inner_region.potential );
	
	PetscInt *global_indices = &occupied_nodes_global_indices[0];
	PetscScalar *values = &phi_inside_region[0];
	
	ierr = VecSetValues( phi_vec, num_of_elements_to_set, global_indices,
			     values, INSERT_VALUES); CHKERRXX( ierr );
	ierr = VecAssemblyBegin( phi_vec ); CHKERRXX( ierr );
	ierr = VecAssemblyEnd( phi_vec ); CHKERRXX( ierr );
    }
    
    return;
}



int Field_solver::kronecker_delta( int i,  int j )
{
    if ( i == j ) {
	return 1;
    } else {
	return 0;
    }
}

int Field_solver::node_global_index_in_matrix( Node_reference &node, int nx, int ny, int nz )
{
    return node_ijk_to_global_index_in_matrix( node.x, node.y, node.z, nx, ny, nz );
}

std::vector<int> Field_solver::list_of_nodes_global_indices_in_matrix( std::vector<Node_reference> &nodes, int nx, int ny, int nz )
{
    std::vector<int> indices;
    indices.reserve( nodes.size() );
    for( auto &node : nodes )
	indices.push_back( node_global_index_in_matrix( node, nx, ny, nz ) );
    return indices;
}

int Field_solver::node_ijk_to_global_index_in_matrix( int i, int j, int k,
						      int nx, int ny, int nz )    
{
    // numbering of nodes corresponds to axis direction
    // i.e. numbering starts from bottom-left-near corner
    //   then along X axis to the right
    //   then along Y axis to the top
    //   then along Z axis far
    if ( ( i <= 0 ) || ( i >= nx-1 ) ||
	 ( j <= 0 ) || ( j >= ny-1 ) ||
	 ( k <= 0 ) || ( k >= nz-1 ) ) {
	printf("incorrect index at node_ijk_to_global_index_in_matrix: i = %d, j=%d, k=%d \n", i,j,k);
	exit( EXIT_FAILURE );
    } else {
	return (i - 1) + (j - 1) * ( nx - 2 ) + ( k - 1 ) * ( nx - 2 ) * ( ny - 2 );
    }    
}

void Field_solver::transfer_solution_to_spat_mesh( Spatial_mesh &spat_mesh )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    PetscScalar phi_at_point;
    PetscInt ix;
    PetscErrorCode ierr;
    
    for( int k = 1; k <= nz-2; k++ ){
	for ( int j = 1; j <= ny-2; j++ ) { 
	    for ( int i = 1; i <= nx-2; i++ ) {
		ix = node_ijk_to_global_index_in_matrix( i, j, k, nx, ny, nz );
		ierr = VecGetValues( phi_vec, 1, &ix, &phi_at_point ); CHKERRXX( ierr );
		spat_mesh.potential[i][j][k] = phi_at_point;
	    }
	}
    }
}


void Field_solver::eval_fields_from_potential( Spatial_mesh &spat_mesh )
{
    int nx = spat_mesh.x_n_nodes;
    int ny = spat_mesh.y_n_nodes;
    int nz = spat_mesh.z_n_nodes;
    double dx = spat_mesh.x_cell_size;
    double dy = spat_mesh.y_cell_size;
    double dz = spat_mesh.z_cell_size;
    boost::multi_array<double, 3> &phi = spat_mesh.potential;
    double ex, ey, ez;

    for ( int i = 0; i < nx; i++ ) {
	for ( int j = 0; j < ny; j++ ) {
	    for ( int k = 0; k < nz; k++ ) {
		if ( i == 0 ) {
		    ex = - boundary_difference( phi[i][j][k], phi[i+1][j][k], dx );
		} else if ( i == nx-1 ) {
		    ex = - boundary_difference( phi[i-1][j][k], phi[i][j][k], dx );
		} else {
		    ex = - central_difference( phi[i-1][j][k], phi[i+1][j][k], dx );
		}

		if ( j == 0 ) {
		    ey = - boundary_difference( phi[i][j][k], phi[i][j+1][k], dy );
		} else if ( j == ny-1 ) {
		    ey = - boundary_difference( phi[i][j-1][k], phi[i][j][k], dy );
		} else {
		    ey = - central_difference( phi[i][j-1][k], phi[i][j+1][k], dy );
		}

		if ( k == 0 ) {
		    ez = - boundary_difference( phi[i][j][k], phi[i][j][k+1], dz );
		} else if ( k == nz-1 ) {
		    ez = - boundary_difference( phi[i][j][k-1], phi[i][j][k], dz );
		} else {
		    ez = - central_difference( phi[i][j][k-1], phi[i][j][k+1], dz );
		}

		spat_mesh.electric_field[i][j][k] = vec3d_init( ex, ey, ez );
	    }
	}
    }

    return;
}

double Field_solver::central_difference( double phi1, double phi2, double dx )
{    
    return ( (phi2 - phi1) / ( 2.0 * dx ) );
}

double Field_solver::boundary_difference( double phi1, double phi2, double dx )
{    
    return ( (phi2 - phi1) / dx );
}


Field_solver::~Field_solver()
{    
    PetscErrorCode ierr;
    ierr = VecDestroy( &phi_vec ); CHKERRXX( ierr );
    ierr = VecDestroy( &rhs ); CHKERRXX( ierr );
    ierr = MatDestroy( &A ); CHKERRXX( ierr );
    ierr = KSPDestroy( &ksp ); CHKERRXX( ierr );
}
