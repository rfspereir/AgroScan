import { Component, Input } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule, Router } from '@angular/router';
import { Auth, signOut } from '@angular/fire/auth';
import { AuthService } from '../../services/auth.service';

@Component({
  selector: 'app-topbar',
  standalone: true,
  imports: [CommonModule, RouterModule],
  templateUrl: './topbar.html',
  styleUrls: ['./topbar.css']
})
export class Topbar {
  
  constructor(private auths: Auth, private router: Router, public auth: AuthService) {}

  @Input() totalClientes: number = 0;
  @Input() totalDispositivos: number = 0;
  @Input() totalUsuarios: number = 0;
  @Input() totalDispositivosPre: number = 0;


  logout() {
    signOut(this.auths).then(() => {
      this.router.navigate(['/']);
      
    });
  }
}
