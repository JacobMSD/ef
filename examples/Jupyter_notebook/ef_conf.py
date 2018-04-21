import os
import tempfile
import subprocess
import configparser

import matplotlib as mpl
from mpl_toolkits.mplot3d import axes3d
import matplotlib.pyplot as plt
from matplotlib import cm

class EfConf:

    def __init__( self ):
        self.time_grid = TimeGrid()
        self.spatial_mesh = SpatialMesh()
        self.sources = []
        self.output_file = OutputFile()
        self.boundary_conditions = BoundaryConditions()
        self.particle_interaction_model = ParticleInteractionModel()

    def add_source( self, src ):
        self.sources.append( src )
        
    def visualize( self ):
        fig = plt.figure()
        ax = fig.add_subplot(111, projection='3d')
        self.time_grid.visualize( ax )
        self.spatial_mesh.visualize( ax )
        for src in self.sources:
            src.visualize( ax )
        #
        ax.set_xlabel('X')
        ax.set_ylabel('Y')
        ax.set_zlabel('Z')
        ax.legend()
        plt.show()


    def export( self, filename ):
        as_dict = {}
        as_dict.update( self.time_grid.export()  )
        as_dict.update( self.spatial_mesh.export() )        
        for src in self.sources:
            as_dict.update( src.export() )
        as_dict.update( self.output_file.export() )
        as_dict.update( self.boundary_conditions.export() )
        as_dict.update( self.particle_interaction_model.export() )
        # can't construct config from dictionary; have to do it manually
        # config = configparser.ConfigParser( as_dict )
        config = configparser.ConfigParser()
        for sec_name, sec in as_dict.items():
            config[sec_name] = {}
            for k, v in sec.items():
                config[sec_name][k] = str(v)
        with open( filename, 'w') as f:
            config.write( f )


    def print_config( self ):
        as_dict = {}
        as_dict.update( self.time_grid.export()  )
        as_dict.update( self.spatial_mesh.export() )        
        for src in self.sources:
            as_dict.update( src.export() )
        as_dict.update( self.output_file.export() )
        as_dict.update( self.boundary_conditions.export() )
        as_dict.update( self.particle_interaction_model.export() )
        for sec_name, sec in as_dict.items():
            print( "[" + sec_name + "]" )
            for k, v in sec.items():
                print( k, " = ", v )
            print()

            
    def run( self, command = "../../ef.out", workdir = "./",
             save_config_as = None, startfile = None ):
        current_dir = os.getcwd()
        os.chdir( workdir )
        if startfile:
            command = command + " " + startfile
        elif save_config_as:
            self.export( save_config_as )
            command = command + " " + save_config_as
        else:
            tmpfile, tmpfilename = tempfile.mkstemp( suffix = ".ini", text = True )
            self.export( tmpfilename )
            command = command + " " + tmpfilename
        print( "command:", command )
        stdout = subprocess.Popen( command, shell = True,
                                   stdout = subprocess.PIPE ).stdout.read()
        print( stdout )
        if tmpfile:
            os.remove( tmpfilename )
        os.chdir( current_dir )


        
class TimeGrid:

    def __init__( self, total_time = 100.0, time_save_step = 10.0, time_step_size = 1.0 ):
        self.total_time = total_time
        self.time_save_step = time_save_step
        self.time_step_size = time_step_size


    def visualize( self, ax ):
        pass


    def export( self ):
        # todo: use representation of class as dict
        as_dict = {}
        as_dict["Time grid"] = {}
        as_dict["Time grid"]["total_time"] = self.total_time        
        as_dict["Time grid"]["time_save_step"] = self.time_save_step
        as_dict["Time grid"]["time_step_size"] = self.time_step_size
        return as_dict
        
    
class SpatialMesh:

    def __init__( self,
                  grid_x_size = 10, grid_x_step = 1,
                  grid_y_size = 10, grid_y_step = 1,
                  grid_z_size = 10, grid_z_step = 1 ):
        self.grid_x_size = grid_x_size
        self.grid_x_step = grid_x_step        
        self.grid_y_size = grid_y_size
        self.grid_y_step = grid_y_step
        self.grid_z_size = grid_z_size
        self.grid_z_step = grid_z_step


    def visualize( self, ax ):
        self.draw_cube( ax )


    def draw_cube( self, ax ):
        vertices = []
        vertices.append( [0, 0, 0] )
        vertices.append( [self.grid_x_size, 0, 0] )
        vertices.append( [self.grid_x_size, self.grid_y_size, 0] )
        vertices.append( [0, self.grid_y_size, 0] )
        vertices.append( [0, 0, 0] )
        vertices.append( [0, 0, self.grid_z_size] )
        vertices.append( [self.grid_x_size, 0, self.grid_z_size] )
        vertices.append( [self.grid_x_size, 0, 0] )
        vertices.append( [self.grid_x_size, 0, self.grid_z_size] )
        vertices.append( [self.grid_x_size, self.grid_y_size, self.grid_z_size] )
        vertices.append( [self.grid_x_size, self.grid_y_size, 0] )
        vertices.append( [self.grid_x_size, self.grid_y_size, self.grid_z_size] )
        vertices.append( [0, self.grid_y_size, self.grid_z_size] )
        vertices.append( [0, self.grid_y_size, 0] )
        vertices.append( [0, self.grid_y_size, self.grid_z_size] )
        vertices.append( [0, 0, self.grid_z_size] )
        x = [ v[0] for v in vertices ]
        y = [ v[1] for v in vertices ]
        z = [ v[2] for v in vertices ]
        ax.plot( x, y, z, label='volume' )


    def export( self ):
        as_dict = {}
        as_dict["Spatial mesh"] = {}
        as_dict["Spatial mesh"]["grid_x_size"] = self.grid_x_size
        as_dict["Spatial mesh"]["grid_x_step"] = self.grid_x_step
        as_dict["Spatial mesh"]["grid_y_size"] = self.grid_y_size
        as_dict["Spatial mesh"]["grid_y_step"] = self.grid_y_step
        as_dict["Spatial mesh"]["grid_z_size"] = self.grid_z_size
        as_dict["Spatial mesh"]["grid_z_step"] = self.grid_z_step
        return as_dict        


class BoundaryConditions:

    def __init__( self,
                  boundary_phi_right = 0, boundary_phi_left = 0,
                  boundary_phi_bottom = 0, boundary_phi_top = 0,
                  boundary_phi_near = 0, boundary_phi_far = 0 ):
        self.boundary_phi_right = boundary_phi_right
        self.boundary_phi_left = boundary_phi_left
        self.boundary_phi_bottom = boundary_phi_bottom
        self.boundary_phi_top = boundary_phi_top
        self.boundary_phi_near = boundary_phi_near
        self.boundary_phi_far = boundary_phi_far


    def visualize( self, ax ):
        pass

    
    def export( self ):
        as_dict = {}        
        as_dict["Boundary conditions"] = {}
        as_dict["Boundary conditions"]["boundary_phi_right"] = self.boundary_phi_right
        as_dict["Boundary conditions"]["boundary_phi_left"] = self.boundary_phi_left
        as_dict["Boundary conditions"]["boundary_phi_bottom"] = self.boundary_phi_bottom
        as_dict["Boundary conditions"]["boundary_phi_top"] = self.boundary_phi_top
        as_dict["Boundary conditions"]["boundary_phi_near"] = self.boundary_phi_near
        as_dict["Boundary conditions"]["boundary_phi_far"] = self.boundary_phi_far
        return as_dict        
    

class ParticleSourceBox():
    def __init__( self, name = 'box_source',
                  initial_number_of_particles = 500,
                  particles_to_generate_each_step = 500, 
                  box_x_left = 6, box_x_right = 5,
                  box_y_bottom = 2, box_y_top = 5,
                  box_z_near = 1, box_z_far = 3,
                  mean_momentum_x = 0,
                  mean_momentum_y = 0,
                  mean_momentum_z = 6.641e-15,
                  temperature = 0.0,
                  charge = -1.799e-6,
                  mass = 3.672e-24 ):
        self.name = name
        self.box_x_left = box_x_left
        self.box_x_right = box_x_right
        self.box_y_bottom = box_y_bottom
        self.box_y_top = box_y_top
        self.box_z_near = box_z_near
        self.box_z_far = box_z_far
        self.initial_number_of_particles = initial_number_of_particles
        self.particles_to_generate_each_step = particles_to_generate_each_step
        self.mean_momentum_x = mean_momentum_x
        self.mean_momentum_y = mean_momentum_y
        self.mean_momentum_z = mean_momentum_z
        self.temperature = temperature
        self.charge = charge
        self.mass = mass


    def visualize( self, ax ):
        self.draw_cube( ax )


    def draw_cube( self, ax ):
        vertices = []
        vertices.append( [self.box_x_left, self.box_y_bottom, self.box_z_near] )
        vertices.append( [self.box_x_right, self.box_y_bottom, self.box_z_near] )
        vertices.append( [self.box_x_right, self.box_y_top, self.box_z_near] )
        vertices.append( [self.box_x_left, self.box_y_top, self.box_z_near] )
        vertices.append( [self.box_x_left, self.box_y_bottom, self.box_z_near] )
        vertices.append( [self.box_x_left, self.box_y_bottom, self.box_z_far] )
        vertices.append( [self.box_x_left, self.box_y_top, self.box_z_far] )
        vertices.append( [self.box_x_left, self.box_y_top, self.box_z_near] )
        vertices.append( [self.box_x_left, self.box_y_top, self.box_z_far] )
        vertices.append( [self.box_x_right, self.box_y_top, self.box_z_far] )
        vertices.append( [self.box_x_right, self.box_y_top, self.box_z_near] )
        vertices.append( [self.box_x_right, self.box_y_top, self.box_z_far] )
        vertices.append( [self.box_x_right, self.box_y_bottom, self.box_z_far] )
        vertices.append( [self.box_x_right, self.box_y_bottom, self.box_z_near] )
        vertices.append( [self.box_x_right, self.box_y_bottom, self.box_z_far] )
        vertices.append( [self.box_x_left, self.box_y_bottom, self.box_z_far] )
        x = [ v[0] for v in vertices ]
        y = [ v[1] for v in vertices ]
        z = [ v[2] for v in vertices ]
        ax.plot( x, y, z, label=self.name )


    def export( self ):
        as_dict = {}
        sec_name = "Particle_source_box" + "." + self.name
        as_dict[sec_name] = {}
        as_dict[sec_name]["box_x_left"] = self.box_x_left
        as_dict[sec_name]["box_x_right"] = self.box_x_right
        as_dict[sec_name]["box_y_bottom"] = self.box_y_bottom
        as_dict[sec_name]["box_y_top"] = self.box_y_top
        as_dict[sec_name]["box_z_near"] = self.box_z_near
        as_dict[sec_name]["box_z_far"] = self.box_z_far
        as_dict[sec_name]["initial_number_of_particles"] = \
                                                self.initial_number_of_particles
        as_dict[sec_name]["particles_to_generate_each_step"] = \
                                                self.particles_to_generate_each_step
        as_dict[sec_name]["mean_momentum_x"] = self.mean_momentum_x
        as_dict[sec_name]["mean_momentum_y"] = self.mean_momentum_y
        as_dict[sec_name]["mean_momentum_z"] = self.mean_momentum_z
        as_dict[sec_name]["temperature"] = self.temperature
        as_dict[sec_name]["charge"] = self.charge
        as_dict[sec_name]["mass"] = self.mass
        return as_dict        


class OutputFile:
    def __init__( self, output_filename_prefix = "out_", output_filename_suffix = ".h5" ):
        self.output_filename_prefix = output_filename_prefix
        self.output_filename_suffix = output_filename_suffix

    def visualize( self, ax ):
        pass


    def export( self ):
        # todo: use representation of class as dict
        as_dict = {}
        as_dict["Output filename"] = {}
        as_dict["Output filename"]["output_filename_prefix"] = self.output_filename_prefix
        as_dict["Output filename"]["output_filename_suffix"] = self.output_filename_suffix
        return as_dict



class ParticleInteractionModel:

    def __init__( self, particle_interaction_model = "PIC" ):
        self.particle_interaction_model = particle_interaction_model

    def visualize( self, ax ):
        pass

    def export( self ):
        # todo: use representation of class as dict
        as_dict = {}
        as_dict["Particle interaction model"] = {}
        as_dict["Particle interaction model"]["particle_interaction_model"] = \
                                                    self.particle_interaction_model

        return as_dict
